/*
 * Logger.cpp — 구조적 로그 구현 [TD-01]  (설계: Docs/Code_Specification.md Logger 절)
 *   ConsoleSink · FileSink · LogBackend(이중 버퍼 큐 + LOG 스레드) · 모드 전환 · 크래시 핸들러
 *
 * ⚠️ ServerCore(PCH)와 LoggerSelfTest(PCH 없음) 양쪽에서 컴파일됩니다.
 *    CorePch.h 의 타입·using namespace std 에 기대지 말고, std::min/max 는 쓰지 마십시오(Windows.h 매크로).
 */

#include "Logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <io.h>
#include <iterator>
#include <mutex>
#include <new>
#include <share.h>
#include <thread>
#include <vector>

// Logger::sMinLevel 갱신 통로 (아래 익명 네임스페이스가 쓰므로 먼저 정의)

struct LoggerAccess
{
	static void SetMinLevel(std::uint8_t level) noexcept
	{
		Logger::sMinLevel.store(level, std::memory_order_relaxed);
	}
};

namespace
{
	/*------------------------------------------------------------------
		상수 · 스레드 로컬
	------------------------------------------------------------------*/

	enum class Mode : std::uint8_t
	{
		Uninitialized,
		Async,
		Draining,
		Sync,
	};

	constexpr const char* kLevelNames[] = { "TRACE", "INFO ", "WARN ", "ERROR", "OFF  " };
	constexpr const char* kCategoryNames[] = { "Sys",  "Config", "Net",   "Session", "Login", "Room",
											   "Aoi",  "Db",     "Redis", "Dummy",   "Test" };
	static_assert(std::size(kCategoryNames) == static_cast<std::size_t>(LogCategory::Count),
				  "LogCategory 와 kCategoryNames 의 개수가 다릅니다");

	constexpr std::size_t kThreadNameMax = 15;
	constexpr DWORD kPanicLockWaitMs = 100;
	constexpr std::uint32_t kFlushWaitMs = 5000;
	constexpr std::uint32_t kDrainWaitMs = 5000;

	thread_local char tThreadName[kThreadNameMax + 1] = {};
	thread_local std::uint32_t tThreadId = 0;
	thread_local bool tInsideLogger = false;  // 로거 내부에서 다시 로그를 부르는 재진입 차단

	std::uint32_t CurrentThreadId() noexcept
	{
		if (tThreadId == 0)
			tThreadId = static_cast<std::uint32_t>(::GetCurrentThreadId());
		return tThreadId;
	}

	LogTimestamp CaptureTimestamp() noexcept
	{
		SYSTEMTIME st;
		::GetLocalTime(&st);
		LogTimestamp t;
		t.year = st.wYear;
		t.month = st.wMonth;
		t.day = st.wDay;
		t.hour = st.wHour;
		t.minute = st.wMinute;
		t.second = st.wSecond;
		t.millis = st.wMilliseconds;
		return t;
	}

	int DayKey(const LogTimestamp& t) noexcept
	{
		return t.year * 10000 + t.month * 100 + t.day;
	}

	// 경로에서 파일 이름만 남깁니다. "D:\...\GameRoom.cpp" → "GameRoom.cpp"
	const char* BaseName(const char* path) noexcept
	{
		if (path == nullptr)
			return "";
		const char* base = path;
		for (const char* p = path; *p != '\0'; ++p)
		{
			if (*p == '\\' || *p == '/')
				base = p + 1;
		}
		return base;
	}

	std::string WideToUtf8(std::wstring_view text)
	{
		if (text.empty())
			return std::string();
		const int srcLen = static_cast<int>(text.size());
		const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), srcLen, nullptr, 0, nullptr, nullptr);
		if (bytes <= 0)
			return std::string();
		std::string out(static_cast<std::size_t>(bytes), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.data(), srcLen, out.data(), bytes, nullptr, nullptr);
		return out;
	}

	std::wstring Utf8ToWide(std::string_view text)
	{
		if (text.empty())
			return std::wstring();
		const int srcLen = static_cast<int>(text.size());
		const int chars = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), srcLen, nullptr, 0);
		if (chars <= 0)
			return std::wstring();
		std::wstring out(static_cast<std::size_t>(chars), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.data(), srcLen, out.data(), chars);
		return out;
	}

	/*------------------------------------------------------------------
		LogRecord — 큐에 들어가는 레코드 1개
	------------------------------------------------------------------*/

	struct LogRecord
	{
		LogLevel level = LogLevel::Info;
		LogCategory category = LogCategory::Sys;
		LogTimestamp time;
		std::uint32_t osThreadId = 0;
		char threadName[kThreadNameMax + 1] = {};
		std::uint64_t seq = 0;
		const char* file = nullptr;  // source_location 의 정적 문자열. 복사하지 않음
		std::uint32_t line = 0;
		std::string body;
	};

	// 본문 정규화: 앞뒤 개행 제거 + 안쪽 제어문자 이스케이프 (1레코드 = 1줄 보장)
	void AppendBody(std::string& out, std::string_view body)
	{
		std::size_t begin = 0;
		std::size_t end = body.size();
		while (begin < end && (body[begin] == '\r' || body[begin] == '\n'))
			++begin;
		while (end > begin && (body[end - 1] == '\r' || body[end - 1] == '\n'))
			--end;

		for (std::size_t i = begin; i < end; ++i)
		{
			const unsigned char c = static_cast<unsigned char>(body[i]);
			if (c >= 0x20 && c != 0x7F)
			{
				out.push_back(static_cast<char>(c));
				continue;
			}
			switch (c)
			{
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
			{
				char hex[8];
				std::snprintf(hex, sizeof(hex), "\\x%02X", static_cast<unsigned>(c));
				out += hex;
				break;
			}
			}
		}
	}

	void AppendThreadTag(std::string& out, const LogRecord& rec, bool withId)
	{
		out.push_back('[');
		out += (rec.threadName[0] != '\0') ? rec.threadName : "T";
		if (withId || rec.threadName[0] == '\0')
		{
			char id[24];
			std::snprintf(id, sizeof(id), " #%u", rec.osThreadId);
			out += id;
		}
		out.push_back(']');
	}

	// 파일 레코드: "2026-09-11 14:03:22.123 INFO  [DB-3 #12345] [Db] 본문[ @파일:줄]\n"
	std::string FormatForFile(const LogRecord& rec)
	{
		std::string out;
		out.reserve(rec.body.size() + 96);

		char head[64];
		std::snprintf(head, sizeof(head), "%04u-%02u-%02u %02u:%02u:%02u.%03u %s ",
					  rec.time.year, rec.time.month, rec.time.day, rec.time.hour, rec.time.minute,
					  rec.time.second, rec.time.millis, kLevelNames[static_cast<std::size_t>(rec.level)]);
		out += head;
		AppendThreadTag(out, rec, true);
		out += " [";
		out += kCategoryNames[static_cast<std::size_t>(rec.category)];
		out += "] ";
		AppendBody(out, rec.body);

		if (rec.level >= LogLevel::Warn && rec.file != nullptr)
		{
			char where[160];
			std::snprintf(where, sizeof(where), " @%s:%u", BaseName(rec.file), rec.line);
			out += where;
		}
		out.push_back('\n');
		return out;
	}

	// 콘솔 레코드: "14:03:22.123 INFO  [DB-3] [Db] 본문\n"
	std::string FormatForConsole(const LogRecord& rec)
	{
		std::string out;
		out.reserve(rec.body.size() + 48);

		char head[32];
		std::snprintf(head, sizeof(head), "%02u:%02u:%02u.%03u %s ", rec.time.hour, rec.time.minute,
					  rec.time.second, rec.time.millis, kLevelNames[static_cast<std::size_t>(rec.level)]);
		out += head;
		AppendThreadTag(out, rec, false);
		out += " [";
		out += kCategoryNames[static_cast<std::size_t>(rec.category)];
		out += "] ";
		AppendBody(out, rec.body);
		out.push_back('\n');
		return out;
	}

	/*------------------------------------------------------------------
		ConsoleSink
	------------------------------------------------------------------*/

	// 콘솔이면 WriteConsoleW(UTF-16, 코드페이지 무관), 리다이렉트면 UTF-8 바이트 그대로
	struct ConsoleSink
	{
		HANDLE out = nullptr;
		bool isConsole = false;

		void Open() noexcept
		{
			out = ::GetStdHandle(STD_OUTPUT_HANDLE);
			DWORD mode = 0;
			isConsole = (out != nullptr && out != INVALID_HANDLE_VALUE && ::GetConsoleMode(out, &mode) != 0);
		}

		void Write(std::string_view utf8) noexcept
		{
			if (out == nullptr || out == INVALID_HANDLE_VALUE || utf8.empty())
				return;

			DWORD written = 0;
			if (isConsole)
			{
				try
				{
					const std::wstring wide = Utf8ToWide(utf8);
					::WriteConsoleW(out, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
				}
				catch (...)
				{
				}
				return;
			}
			::WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
		}
	};

	/*------------------------------------------------------------------
		FileSink
	------------------------------------------------------------------*/

	struct FileSink
	{
		FILE* file = nullptr;
		int day = -1;
		wchar_t dir[MAX_PATH] = {};
		char program[32] = {};
		wchar_t currentName[MAX_PATH] = {};
		std::uint32_t retentionDays = 14;

		// 경로의 각 단계 폴더를 순서대로 만듭니다(드라이브 루트 제외, 이미 있으면 통과).
		static void CreateDirectories(const wchar_t* path) noexcept
		{
			wchar_t partial[MAX_PATH] = {};
			const std::size_t len = ::wcsnlen(path, MAX_PATH - 1);
			for (std::size_t i = 0; i < len; ++i)
			{
				const bool sep = (path[i] == L'\\' || path[i] == L'/');
				if (sep && i > 0 && path[i - 1] != L':')
				{
					partial[i] = L'\0';
					::CreateDirectoryW(partial, nullptr);
				}
				partial[i] = path[i];
			}
			partial[len] = L'\0';
			if (len > 0)
				::CreateDirectoryW(partial, nullptr);
		}

		bool Open(const LogTimestamp& t) noexcept
		{
			CreateDirectories(dir);

			wchar_t name[MAX_PATH] = {};
			std::swprintf(name, MAX_PATH, L"%ls\\%hs_%04u%02u%02u_%02u%02u%02u_p%lu.log", dir, program,
						  t.year, t.month, t.day, t.hour, t.minute, t.second, ::GetCurrentProcessId());

			// 다른 프로세스의 읽기는 허용, 쓰기는 차단. 바이너리 모드(LF 유지).
			file = ::_wfsopen(name, L"ab", _SH_DENYWR);
			if (file == nullptr)
				return false;

			::wcsncpy_s(currentName, name, _TRUNCATE);
			day = DayKey(t);
			return true;
		}

		void Close() noexcept
		{
			if (file != nullptr)
			{
				std::fflush(file);
				std::fclose(file);
				file = nullptr;
			}
		}

		// 자정이 지나면 새 파일로 넘어갑니다.
		void RollIfNeeded(const LogTimestamp& t) noexcept
		{
			if (file == nullptr || DayKey(t) == day)
				return;
			Close();
			if (Open(t))
				PurgeOld();
		}

		void Write(std::string_view line, const LogTimestamp& t) noexcept
		{
			RollIfNeeded(t);
			if (file != nullptr)
				std::fwrite(line.data(), 1, line.size(), file);
		}

		// durable: 디스크까지 flush (비쌈 — ERROR·종료 시에만)
		void Flush(bool durable) noexcept
		{
			if (file == nullptr)
				return;
			std::fflush(file);
			if (durable)
			{
				const HANDLE h = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(file)));
				if (h != INVALID_HANDLE_VALUE)
					::FlushFileBuffers(h);
			}
		}

		// 와일드카드는 8.3 짧은 이름에도 맞을 수 있어 긴 이름으로 한 번 더 확인합니다.
		bool IsOwnLogFile(const wchar_t* fileName) const noexcept
		{
			const std::wstring prefix = Utf8ToWide(program) + L"_";
			const std::size_t len = ::wcslen(fileName);
			if (len < prefix.size() + 4)
				return false;
			if (::wcsncmp(fileName, prefix.c_str(), prefix.size()) != 0)
				return false;
			if (::_wcsicmp(fileName + len - 4, L".log") != 0)
				return false;
			return ::wcsstr(fileName, L"_p") != nullptr;
		}

		// 보존 기간이 지난 <program>_*_p*.log 삭제 (현재 파일 제외).
		// ⚠️ 다른 파일을 지우지 않도록 패턴을 넓히지 마십시오.
		void PurgeOld() noexcept
		{
			if (retentionDays == 0)
				return;

			wchar_t pattern[MAX_PATH] = {};
			std::swprintf(pattern, MAX_PATH, L"%ls\\%hs_*_p*.log", dir, program);

			FILETIME nowFt;
			::GetSystemTimeAsFileTime(&nowFt);
			ULARGE_INTEGER now;
			now.LowPart = nowFt.dwLowDateTime;
			now.HighPart = nowFt.dwHighDateTime;
			const unsigned long long limit = static_cast<unsigned long long>(retentionDays) * 24ULL * 3600ULL * 10000000ULL;

			WIN32_FIND_DATAW data;
			const HANDLE find = ::FindFirstFileW(pattern, &data);
			if (find == INVALID_HANDLE_VALUE)
				return;
			do
			{
				if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
					continue;
				if (IsOwnLogFile(data.cFileName) == false)
					continue;

				ULARGE_INTEGER written;
				written.LowPart = data.ftLastWriteTime.dwLowDateTime;
				written.HighPart = data.ftLastWriteTime.dwHighDateTime;
				if (now.QuadPart <= written.QuadPart || now.QuadPart - written.QuadPart <= limit)
					continue;

				wchar_t full[MAX_PATH] = {};
				std::swprintf(full, MAX_PATH, L"%ls\\%ls", dir, data.cFileName);
				if (::_wcsicmp(full, currentName) != 0)
					::DeleteFileW(full);
			} while (::FindNextFileW(find, &data));
			::FindClose(find);
		}
	};

	/*------------------------------------------------------------------
		LogBackend — 이중 버퍼 큐 + 기록 스레드
	------------------------------------------------------------------*/

	enum class EnqueueResult : std::uint8_t
	{
		Enqueued,
		Dropped,     // TRACE 이고 큐가 가득 참 (안전장치 ④)
		NotRunning,  // 기록 스레드가 없음 → 호출자는 동기 경로로
	};

	struct LogBackend
	{
		std::mutex queueLock;
		std::condition_variable cvWork;     // 기록 스레드 깨우기
		std::condition_variable cvSpace;    // 큐에 자리가 났음
		std::condition_variable cvFlushed;  // flushedSeq 가 올라감
		std::vector<LogRecord> front;       // 생산자가 채우는 버퍼
		std::vector<LogRecord> back;        // 기록 스레드가 비우는 버퍼
		std::uint64_t enqueuedSeq = 0;      // 마지막으로 발급한 순번
		std::uint64_t flushedSeq = 0;       // 기록이 끝난 마지막 순번
		std::uint64_t droppedTotal = 0;
		std::uint64_t droppedReported = 0;
		std::uint32_t capacity = 8192;
		bool running = false;
		bool stopping = false;
		bool errorPending = false;  // 이번 배치에 ERROR 가 있음 → 디스크까지 flush
		std::jthread thread;
	};

	/*------------------------------------------------------------------
		LoggerState — 정적 저장소에 두고 소멸시키지 않음 (F-4)
	------------------------------------------------------------------*/

	struct LoggerState
	{
		std::atomic<std::uint8_t> consoleLevel{ static_cast<std::uint8_t>(LogLevel::Info) };
		std::atomic<std::uint8_t> fileLevel{ static_cast<std::uint8_t>(LogLevel::Info) };
		std::atomic<bool> fileEnabled{ true };
		std::uint32_t errorWaitMs = 1000;
		char program[32] = {};

		std::mutex writeLock;  // 싱크 쓰기 직렬화 (기록 스레드 또는 동기 모드 호출자)
		ConsoleSink console;
		FileSink file;
		LogBackend backend;

		std::mutex modeLock;
		std::condition_variable modeCv;  // Draining → Sync 전환 알림
	};

	alignas(LoggerState) unsigned char gStateStorage[sizeof(LoggerState)];
	LoggerState* gState = nullptr;
	std::atomic<Mode> gMode{ Mode::Uninitialized };
	std::atomic<bool> gInitStarted{ false };
	std::atomic<bool> gPanicking{ false };
	std::mutex gPreInitLock;

	LPTOP_LEVEL_EXCEPTION_FILTER gPrevExceptionFilter = nullptr;
	std::terminate_handler gPrevTerminate = nullptr;
	_crt_signal_t gPrevAbortHandler = nullptr;

	/*------------------------------------------------------------------
		쓰기 경로
	------------------------------------------------------------------*/

	bool WantsFile(const LoggerState& s, LogLevel level) noexcept
	{
		return s.fileEnabled.load(std::memory_order_relaxed) &&
			   static_cast<std::uint8_t>(level) >= s.fileLevel.load(std::memory_order_relaxed);
	}

	bool WantsConsole(const LoggerState& s, LogLevel level) noexcept
	{
		return static_cast<std::uint8_t>(level) >= s.consoleLevel.load(std::memory_order_relaxed);
	}

	// writeLock 을 잡은 상태에서 호출합니다.
	void WriteRecordLocked(LoggerState& s, const LogRecord& rec) noexcept
	{
		try
		{
			if (WantsFile(s, rec.level))
				s.file.Write(FormatForFile(rec), rec.time);
			if (WantsConsole(s, rec.level))
				s.console.Write(FormatForConsole(rec));
		}
		catch (...)
		{
		}
	}

	LogRecord MakeRecord(LogLevel level, LogCategory category, std::string body) noexcept
	{
		LogRecord rec;
		rec.level = level;
		rec.category = category;
		rec.time = CaptureTimestamp();
		rec.osThreadId = CurrentThreadId();
		::strncpy_s(rec.threadName, tThreadName, _TRUNCATE);
		try
		{
			rec.body = std::move(body);
		}
		catch (...)
		{
		}
		return rec;
	}

	void RecomputeMinLevel(const LoggerState& s) noexcept;

	/*------------------------------------------------------------------
		기록 스레드
	------------------------------------------------------------------*/

	void BackendThreadMain(LoggerState& s)
	{
		Logger::SetThreadName("LOG");
		LogBackend& b = s.backend;

		while (true)
		{
			std::unique_lock<std::mutex> lk(b.queueLock);
			b.cvWork.wait_for(lk, std::chrono::milliseconds(200), [&b] { return !b.front.empty() || b.stopping; });

			if (b.front.empty())
			{
				if (b.stopping)
					break;
				continue;  // 주기적으로 깨어났으나 할 일 없음
			}

			b.front.swap(b.back);
			const bool durable = b.errorPending;
			b.errorPending = false;
			const std::uint64_t dropped = b.droppedTotal - b.droppedReported;
			b.droppedReported = b.droppedTotal;
			const std::uint64_t lastSeq = b.back.back().seq;
			lk.unlock();
			b.cvSpace.notify_all();

			{
				std::lock_guard<std::mutex> wl(s.writeLock);
				for (const LogRecord& rec : b.back)
					WriteRecordLocked(s, rec);

				if (dropped > 0)
				{
					LogRecord note = MakeRecord(LogLevel::Warn, LogCategory::Sys,
												"dropped " + std::to_string(dropped) + " TRACE records (queue full)");
					WriteRecordLocked(s, note);
				}
				s.file.Flush(durable);
			}
			b.back.clear();

			{
				std::lock_guard<std::mutex> relock(b.queueLock);
				b.flushedSeq = lastSeq;
			}
			b.cvFlushed.notify_all();
		}
	}

	void StartBackend(LoggerState& s, std::uint32_t capacity)
	{
		LogBackend& b = s.backend;
		b.capacity = (capacity == 0) ? 1 : capacity;
		b.front.reserve(b.capacity);
		b.back.reserve(b.capacity);
		b.running = true;
		b.stopping = false;
		b.thread = std::jthread([&s](std::stop_token) { BackendThreadMain(s); });
	}

	// 큐를 끝까지 비우고 기록 스레드를 멈춘 뒤 버퍼·스레드 상태를 해제합니다(누수 덤프 방지).
	void StopAndDrain(LoggerState& s) noexcept
	{
		LogBackend& b = s.backend;
		{
			std::lock_guard<std::mutex> lk(b.queueLock);
			if (!b.running)
				return;
			b.stopping = true;
		}
		b.cvWork.notify_all();

		if (b.thread.joinable())
			b.thread.join();

		{
			std::lock_guard<std::mutex> lk(b.queueLock);
			b.running = false;
			b.flushedSeq = b.enqueuedSeq;
			std::vector<LogRecord>().swap(b.front);
			std::vector<LogRecord>().swap(b.back);
		}
		b.cvSpace.notify_all();
		b.cvFlushed.notify_all();
		b.thread = std::jthread();  // stop_state 공유 블록 해제
	}

	EnqueueResult Enqueue(LoggerState& s, LogRecord& rec, std::uint64_t& seqOut) noexcept
	{
		LogBackend& b = s.backend;
		bool wake = false;
		{
			std::unique_lock<std::mutex> lk(b.queueLock);
			if (!b.running)
				return EnqueueResult::NotRunning;

			if (b.front.size() >= b.capacity)
			{
				if (rec.level == LogLevel::Trace)
				{
					++b.droppedTotal;
					return EnqueueResult::Dropped;
				}
				b.cvSpace.wait(lk, [&b] { return b.front.size() < b.capacity || !b.running; });
				if (!b.running)
					return EnqueueResult::NotRunning;
			}

			rec.seq = ++b.enqueuedSeq;
			seqOut = rec.seq;
			if (rec.level >= LogLevel::Error)
				b.errorPending = true;
			wake = b.front.empty() || rec.level >= LogLevel::Error;
			try
			{
				b.front.push_back(std::move(rec));
			}
			catch (...)
			{
				--b.enqueuedSeq;
				return EnqueueResult::NotRunning;
			}
		}
		if (wake)
			b.cvWork.notify_one();
		return EnqueueResult::Enqueued;
	}

	void WaitFlushed(LoggerState& s, std::uint64_t seq, std::uint32_t timeoutMs) noexcept
	{
		LogBackend& b = s.backend;
		std::unique_lock<std::mutex> lk(b.queueLock);
		b.cvFlushed.wait_for(lk, std::chrono::milliseconds(timeoutMs),
							 [&b, seq] { return b.flushedSeq >= seq || !b.running; });
	}

	/*------------------------------------------------------------------
		모드 전환
	------------------------------------------------------------------*/

	// Async → Draining → Sync. atexit 와 Configure(async=false) 가 부릅니다.
	void SwitchToSync() noexcept
	{
		Mode expected = Mode::Async;
		if (!gMode.compare_exchange_strong(expected, Mode::Draining))
			return;

		StopAndDrain(*gState);

		{
			std::lock_guard<std::mutex> lk(gState->modeLock);
			gMode.store(Mode::Sync);
		}
		gState->modeCv.notify_all();
	}

	void WaitWhileDraining() noexcept
	{
		if (gMode.load() != Mode::Draining)
			return;
		std::unique_lock<std::mutex> lk(gState->modeLock);
		gState->modeCv.wait_for(lk, std::chrono::milliseconds(kDrainWaitMs),
								[] { return gMode.load() != Mode::Draining; });
	}

	void WriteDirect(LoggerState& s, const LogRecord& rec) noexcept
	{
		std::lock_guard<std::mutex> wl(s.writeLock);
		WriteRecordLocked(s, rec);
		s.file.Flush(rec.level >= LogLevel::Error);
	}

	// Init 전: 파일 없이 콘솔에만 씁니다.
	void WritePreInit(const LogRecord& rec) noexcept
	{
		std::lock_guard<std::mutex> lk(gPreInitLock);
		ConsoleSink console;
		console.Open();
		try
		{
			console.Write(FormatForConsole(rec));
		}
		catch (...)
		{
		}
	}

	// atexit: 정적 소멸자보다 먼저 실행되어 이후 로그를 동기로 받습니다.
	void OnProcessExit() noexcept
	{
		if (gState == nullptr)
			return;
		SwitchToSync();
		std::lock_guard<std::mutex> wl(gState->writeLock);
		gState->file.Flush(true);
	}

	/*------------------------------------------------------------------
		크래시 핸들러 (안전장치 ②) — best-effort
	------------------------------------------------------------------*/

	bool TryLockFor(std::mutex& m, DWORD waitMs) noexcept
	{
		const ULONGLONG deadline = ::GetTickCount64() + waitMs;
		while (true)
		{
			if (m.try_lock())
				return true;
			if (::GetTickCount64() >= deadline)
				return false;
			::Sleep(1);
		}
	}

	// 큐 잔여분 + PANIC 레코드를 직접 기록. 락은 100ms 만 시도하고, 첫 호출만 동작합니다.
	void PanicFlush(const char* reason) noexcept
	{
		if (gState == nullptr || gPanicking.exchange(true))
			return;

		LoggerState& s = *gState;
		std::vector<LogRecord> pending;

		const bool gotQueue = TryLockFor(s.backend.queueLock, kPanicLockWaitMs);
		if (gotQueue)
		{
			try
			{
				pending.swap(s.backend.front);
			}
			catch (...)
			{
			}
			s.backend.queueLock.unlock();
		}

		const bool gotWrite = TryLockFor(s.writeLock, kPanicLockWaitMs);
		for (const LogRecord& rec : pending)
			WriteRecordLocked(s, rec);

		LogRecord panic = MakeRecord(LogLevel::Error, LogCategory::Sys, std::string("PANIC: ") + reason);
		WriteRecordLocked(s, panic);
		s.file.Flush(true);
		if (gotWrite)
			s.writeLock.unlock();
	}

	constexpr DWORD kMsvcCppExceptionCode = 0xE06D7363;  // MSVC 가 C++ throw 를 SEH 로 표현할 때의 코드 ('msc')

	LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
	{
		const DWORD code = (info != nullptr && info->ExceptionRecord != nullptr) ? info->ExceptionRecord->ExceptionCode : 0UL;

		// C++ 예외는 CRT 필터(→ std::terminate → OnTerminate)로 먼저 넘겨 e.what() 을 남깁니다.
		if (code == kMsvcCppExceptionCode && gPrevExceptionFilter != nullptr)
		{
			const LONG result = gPrevExceptionFilter(info);
			PanicFlush("unhandled C++ exception (SEH 0xE06D7363)");
			return result;
		}

		char reason[96];
		std::snprintf(reason, sizeof(reason), "unhandled SEH exception 0x%08lX", code);
		PanicFlush(reason);
		return (gPrevExceptionFilter != nullptr) ? gPrevExceptionFilter(info) : EXCEPTION_CONTINUE_SEARCH;
	}

	void OnTerminate()
	{
		std::string reason = "std::terminate (uncaught C++ exception)";
		try
		{
			if (std::exception_ptr current = std::current_exception())
				std::rethrow_exception(current);
		}
		catch (const std::exception& e)
		{
			reason += ": ";
			reason += e.what();
		}
		catch (...)
		{
		}
		PanicFlush(reason.c_str());

		if (gPrevTerminate != nullptr)
			gPrevTerminate();
		std::abort();
	}

	void __cdecl OnAbortSignal(int)
	{
		PanicFlush("SIGABRT (abort)");
		// 핸들러가 반환하면 CRT abort 가 프로세스 종료를 이어갑니다.
	}

	void InstallCrashHandlers() noexcept
	{
		gPrevExceptionFilter = ::SetUnhandledExceptionFilter(&OnUnhandledException);
		gPrevTerminate = std::set_terminate(&OnTerminate);
		gPrevAbortHandler = std::signal(SIGABRT, &OnAbortSignal);
	}

	void RecomputeMinLevel(const LoggerState& s) noexcept
	{
		std::uint8_t minLevel = s.consoleLevel.load();
		if (s.fileEnabled.load())
		{
			const std::uint8_t f = s.fileLevel.load();
			if (f < minLevel)
				minLevel = f;
		}
		LoggerAccess::SetMinLevel(minLevel);
	}
} // namespace

/*----------------------------------------------------------------------
	Logger
----------------------------------------------------------------------*/

void Logger::Init(const LogOptions& options) noexcept
{
	if (gInitStarted.exchange(true))
		return;

	LoggerState* s = ::new (static_cast<void*>(gStateStorage)) LoggerState();
	s->consoleLevel.store(static_cast<std::uint8_t>(options.consoleLevel));
	s->fileLevel.store(static_cast<std::uint8_t>(options.fileLevel));
	s->fileEnabled.store(options.fileEnabled);
	s->errorWaitMs = options.errorWaitMs;

	const std::size_t programLen = options.programName.size() < sizeof(s->program) - 1
									   ? options.programName.size()
									   : sizeof(s->program) - 1;
	std::memcpy(s->program, options.programName.data(), programLen);
	s->program[programLen] = '\0';

	s->console.Open();

	bool fileOpened = false;
	if (options.fileEnabled)
	{
		std::memcpy(s->file.program, s->program, sizeof(s->file.program));
		s->file.retentionDays = options.retentionDays;
		const std::wstring dirW = Utf8ToWide(options.directory);
		::wcsncpy_s(s->file.dir, dirW.c_str(), _TRUNCATE);
		fileOpened = s->file.Open(CaptureTimestamp());
		if (fileOpened)
			s->file.PurgeOld();
		else
			s->fileEnabled.store(false);
	}

	gState = s;
	RecomputeMinLevel(*s);

	if (options.async)
	{
		StartBackend(*s, options.queueCapacity);
		gMode.store(Mode::Async);
	}
	else
	{
		gMode.store(Mode::Sync);
	}

	std::atexit(&OnProcessExit);
	if (options.installCrashHandlers)
		InstallCrashHandlers();

	std::string opened = std::string("Log opened: program=") + s->program +
						 " pid=" + std::to_string(::GetCurrentProcessId()) +
						 " mode=" + (options.async ? "async" : "sync") +
#ifdef _DEBUG
						 " build=Debug" +
#else
						 " build=Release" +
#endif
						 " file=" + (fileOpened ? WideToUtf8(s->file.currentName) : std::string("(none)"));
	Submit(LogLevel::Info, LogCategory::Sys, std::source_location::current(), CaptureTimestamp(), std::move(opened));
}

void Logger::Configure(const LogSettings& settings) noexcept
{
	if (gState == nullptr)
		return;
	LoggerState& s = *gState;

	s.consoleLevel.store(static_cast<std::uint8_t>(settings.consoleLevel));
	s.fileLevel.store(static_cast<std::uint8_t>(settings.fileLevel));

	if (!settings.fileEnabled && s.fileEnabled.load())
	{
		std::lock_guard<std::mutex> wl(s.writeLock);
		s.fileEnabled.store(false);
		s.file.Close();
	}
	RecomputeMinLevel(s);

	if (!settings.async)
		SwitchToSync();

	const Mode mode = gMode.load();
	std::string msg = std::string("Log configured: mode=") + (mode == Mode::Async ? "async" : "sync") +
					  " file=" + (s.fileEnabled.load() ? "on" : "off") +
					  " consoleLevel=" + kLevelNames[static_cast<std::size_t>(settings.consoleLevel)] +
					  " fileLevel=" + kLevelNames[static_cast<std::size_t>(settings.fileLevel)];
	Submit(LogLevel::Info, LogCategory::Sys, std::source_location::current(), CaptureTimestamp(), std::move(msg));
}

void Logger::Flush() noexcept
{
	if (gState == nullptr)
		return;
	LoggerState& s = *gState;

	if (gMode.load() == Mode::Async)
	{
		std::uint64_t target = 0;
		{
			std::lock_guard<std::mutex> lk(s.backend.queueLock);
			target = s.backend.enqueuedSeq;
		}
		WaitFlushed(s, target, kFlushWaitMs);
	}

	WaitWhileDraining();
	std::lock_guard<std::mutex> wl(s.writeLock);
	s.file.Flush(false);
}

void Logger::SetThreadName(std::string_view name) noexcept
{
	std::size_t n = 0;
	for (; n < name.size() && n < kThreadNameMax; ++n)
	{
		const char c = name[n];
		const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
		tThreadName[n] = ok ? c : '-';
	}
	tThreadName[n] = '\0';

	// SetThreadDescription 은 Windows 10 1607+ 전용이라 동적으로 찾습니다.
	using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);
	static const SetThreadDescriptionFn setDescription = []() -> SetThreadDescriptionFn {
		const HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
		if (kernel == nullptr)
			return nullptr;
		return reinterpret_cast<SetThreadDescriptionFn>(::GetProcAddress(kernel, "SetThreadDescription"));
	}();
	if (setDescription != nullptr)
	{
		wchar_t wide[kThreadNameMax + 1] = {};
		for (std::size_t i = 0; i < n; ++i)
			wide[i] = static_cast<wchar_t>(tThreadName[i]);
		setDescription(::GetCurrentThread(), wide);
	}
}

void Logger::SetThreadName(std::string_view prefix, int index) noexcept
{
	char name[kThreadNameMax + 1] = {};
	std::snprintf(name, sizeof(name), "%.*s-%d", static_cast<int>(prefix.size()), prefix.data(), index);
	SetThreadName(std::string_view(name));
}

LogLevel Logger::ParseLevel(std::string_view text, LogLevel fallback) noexcept
{
	auto equals = [text](const char* word) {
		const std::size_t len = std::strlen(word);
		if (text.size() != len)
			return false;
		for (std::size_t i = 0; i < len; ++i)
		{
			char c = text[i];
			if (c >= 'A' && c <= 'Z')
				c = static_cast<char>(c - 'A' + 'a');
			if (c != word[i])
				return false;
		}
		return true;
	};

	if (equals("trace"))
		return LogLevel::Trace;
	if (equals("info"))
		return LogLevel::Info;
	if (equals("warn") || equals("warning"))
		return LogLevel::Warn;
	if (equals("error"))
		return LogLevel::Error;
	if (equals("off"))
		return LogLevel::Off;
	return fallback;
}

std::uint64_t Logger::DroppedCount() noexcept
{
	if (gState == nullptr)
		return 0;
	std::lock_guard<std::mutex> lk(gState->backend.queueLock);
	return gState->backend.droppedTotal;
}

void Logger::Submit(LogLevel level, LogCategory category, const std::source_location& location,
					const LogTimestamp& time, std::string&& body) noexcept
{
	if (tInsideLogger)
		return;
	tInsideLogger = true;

	try
	{
		LogRecord rec;
		rec.level = level;
		rec.category = category;
		rec.time = time;
		rec.osThreadId = CurrentThreadId();
		::strncpy_s(rec.threadName, tThreadName, _TRUNCATE);
		rec.file = location.file_name();
		rec.line = location.line();
		rec.body = std::move(body);

		const Mode mode = gMode.load();
		if (mode == Mode::Uninitialized || gState == nullptr)
		{
			WritePreInit(rec);
			tInsideLogger = false;
			return;
		}

		LoggerState& s = *gState;
		if (mode == Mode::Async)
		{
			std::uint64_t seq = 0;
			const EnqueueResult result = Enqueue(s, rec, seq);
			if (result == EnqueueResult::Enqueued)
			{
				if (level >= LogLevel::Error)
					WaitFlushed(s, seq, s.errorWaitMs);  // 안전장치 ①
				tInsideLogger = false;
				return;
			}
			if (result == EnqueueResult::Dropped)
			{
				tInsideLogger = false;
				return;
			}
			// NotRunning: 전환 중이므로 아래 동기 경로로
		}

		WaitWhileDraining();
		WriteDirect(s, rec);
	}
	catch (...)
	{
	}
	tInsideLogger = false;
}

/*----------------------------------------------------------------------
	LogLine
----------------------------------------------------------------------*/

LogLine::LogLine(LogLevel level, LogCategory category, std::source_location location) noexcept
	: _level(level), _category(category), _location(location), _time(CaptureTimestamp())
{
}

LogLine::~LogLine()
{
	try
	{
		Logger::Submit(_level, _category, _location, _time, _stream.str());
	}
	catch (...)
	{
	}
}

LogLine& LogLine::operator<<(const wchar_t* text) noexcept
{
	if (text == nullptr)
		return *this;
	return *this << std::wstring_view(text);
}

LogLine& LogLine::operator<<(std::wstring_view text) noexcept
{
	try
	{
		_stream << WideToUtf8(text);
	}
	catch (...)
	{
	}
	return *this;
}

LogLine& LogLine::operator<<(wchar_t ch) noexcept
{
	return *this << std::wstring_view(&ch, 1);
}

LogLine& LogLine::operator<<(std::ostream& (*manip)(std::ostream&)) noexcept
{
	try
	{
		manip(_stream);
	}
	catch (...)
	{
	}
	return *this;
}

LogLine& LogLine::operator<<(std::ios_base& (*manip)(std::ios_base&)) noexcept
{
	try
	{
		manip(_stream);
	}
	catch (...)
	{
	}
	return *this;
}

/*----------------------------------------------------------------------
	LogMask
----------------------------------------------------------------------*/

std::string LogMask::Ticket(std::string_view ticket)
{
	const std::size_t keep = ticket.size() > 8 ? 8 : ticket.size() / 2;
	std::string out(ticket.substr(0, keep));
	out += "...(len=";
	out += std::to_string(ticket.size());
	out += ")";
	return out;
}

std::string LogMask::Uri(std::string_view uri)
{
	const std::size_t scheme = uri.find("://");
	const std::size_t authorityBegin = (scheme == std::string_view::npos) ? 0 : scheme + 3;
	std::size_t authorityEnd = uri.find('/', authorityBegin);
	if (authorityEnd == std::string_view::npos)
		authorityEnd = uri.size();

	const std::string_view authority = uri.substr(authorityBegin, authorityEnd - authorityBegin);
	const std::size_t at = authority.rfind('@');
	if (at == std::string_view::npos)
		return std::string(uri);

	const std::string_view userInfo = authority.substr(0, at);
	const std::size_t colon = userInfo.find(':');

	std::string out(uri.substr(0, authorityBegin));
	if (colon == std::string_view::npos)
		out += "***";  // 비밀번호 단독 형식 (tcp://pass@host)
	else
	{
		out += userInfo.substr(0, colon);
		out += ":***";
	}
	out += uri.substr(authorityBegin + at);  // "@host..." 부터 끝까지
	return out;
}
