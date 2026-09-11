#pragma once

/*
 * Logger — 구조적 로그 [TD-01]
 *   MLOG_INFO(Db) << ... ; 한 문장이 레코드 1개가 됩니다.
 *   레코드: 시각(ms) · 레벨 · 스레드 이름 + OS TID · 카테고리 · 본문
 *   기본은 전용 기록 스레드(LOG)가 쓰는 비동기, Config "Log.Async": false 면 동기.
 *   상세 설계·결정 근거: Docs/Code_Specification.md (Logger 절)
 *
 * ⚠️ 이 헤더는 CorePch.h·Windows.h 없이 컴파일돼야 합니다(LoggerSelfTest 가 단독 컴파일).
 * ⚠️ 로거 상태를 함수 내부 static 으로 바꾸지 마십시오. main() 이후 정적 소멸 구간 로그가 사라집니다.
 */

#include <atomic>
#include <cstdint>
#include <ios>
#include <ostream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

// 설정한 레벨 이상만 기록됩니다.
enum class LogLevel : std::uint8_t
{
	Trace = 0,  // 매우 잦은 이벤트. 큐가 차면 버려질 수 있음
	Info = 1,
	Warn = 2,
	Error = 3,  // 호출자가 파일 기록 완료까지 대기 → 유실 없음
	Off = 4,    // 필터 전용
};

// 레코드 출처 태그. 값을 추가하면 Logger.cpp 의 kCategoryNames 도 늘릴 것.
enum class LogCategory : std::uint8_t
{
	Sys,
	Config,
	Net,
	Session,
	Login,
	Room,
	Aoi,
	Db,
	Redis,
	Dummy,
	Test,
	Count,
};

// 로그 문장이 시작된 시점의 지역 시각
struct LogTimestamp
{
	std::uint16_t year = 0;
	std::uint16_t month = 0;
	std::uint16_t day = 0;
	std::uint16_t hour = 0;
	std::uint16_t minute = 0;
	std::uint16_t second = 0;
	std::uint16_t millis = 0;
};

// Logger::Init 인자. 예) Logger::Init({ .programName = "DummyClient", .async = false });
struct LogOptions
{
	std::string_view programName = "Server";  // 로그 파일 이름 접두어 (최대 31자)
	std::string_view directory = "Logs";      // 상대 경로면 작업 폴더 기준
	bool async = true;
	bool fileEnabled = true;
	LogLevel consoleLevel = LogLevel::Info;
	LogLevel fileLevel = LogLevel::Info;
	std::uint32_t queueCapacity = 8192;       // 비동기 큐 한 버퍼의 최대 레코드 수
	std::uint32_t retentionDays = 14;         // 이보다 오래된 <programName>_*_p*.log 삭제
	std::uint32_t errorWaitMs = 1000;         // ERROR 호출자의 최대 대기 시간
	bool installCrashHandlers = true;         // SEH · std::terminate · SIGABRT
};

// Config.json "Log" 섹션. Logger::Configure 로 적용합니다.
struct LogSettings
{
	bool async = true;
	bool fileEnabled = true;
	LogLevel consoleLevel = LogLevel::Info;
	LogLevel fileLevel = LogLevel::Info;
};

class Logger
{
public:
	// main() 첫 줄에서 1회. 파일 열기 · 기록 스레드 시작 · atexit/크래시 핸들러 등록.
	static void Init(const LogOptions& options) noexcept;

	// 레벨 변경, async→sync 전환, 파일 끄기만 가능합니다(되돌리기 불가).
	static void Configure(const LogSettings& settings) noexcept;

	// 지금까지 넣은 레코드가 기록될 때까지 대기(최대 5초).
	static void Flush() noexcept;

	// 이 레벨을 기록할 곳이 있으면 true. 매크로가 인자 평가 전에 호출합니다.
	static bool ShouldLog(LogLevel level) noexcept
	{
		return static_cast<std::uint8_t>(level) >= sMinLevel.load(std::memory_order_relaxed);
	}

	// 현재 스레드의 표기 이름(최대 15자). 스레드 함수 첫 줄에서 호출합니다.
	static void SetThreadName(std::string_view name) noexcept;
	static void SetThreadName(std::string_view prefix, int index) noexcept;  // ("DB", 3) → "DB-3"

	static LogLevel ParseLevel(std::string_view text, LogLevel fallback) noexcept;
	static std::uint64_t DroppedCount() noexcept;  // 큐가 넘쳐 버린 TRACE 누적 수

	// LogLine 전용. 직접 호출하지 마십시오.
	static void Submit(LogLevel level, LogCategory category, const std::source_location& location,
					   const LogTimestamp& time, std::string&& body) noexcept;

private:
	// min(콘솔 레벨, 파일 레벨). 상수 초기화되므로 Init 전에도 안전합니다.
	inline static std::atomic<std::uint8_t> sMinLevel{ static_cast<std::uint8_t>(LogLevel::Info) };
	friend struct LoggerAccess;
};

// 와이드 문자열 타입 → LogLine 의 UTF-8 변환 오버로드로 보냅니다.
template <typename T>
concept WideText = std::is_convertible_v<const T&, std::wstring_view> ||
				   std::is_same_v<std::remove_cvref_t<T>, wchar_t>;

// std::ostream 으로 출력 가능한 일반 타입
template <typename T>
concept NarrowLoggable = !WideText<T> && requires(std::ostream& os, const T& v) { os << v; };

// 문장 1개 = 레코드 1개. << 로 본문을 모으고 소멸자(문장 끝)에서 Submit 합니다.
class LogLine
{
public:
	LogLine(LogLevel level, LogCategory category, std::source_location location) noexcept;
	~LogLine();

	LogLine(const LogLine&) = delete;
	LogLine& operator=(const LogLine&) = delete;

	template <NarrowLoggable T>
	LogLine& operator<<(const T& value) noexcept
	{
		try
		{
			_stream << value;
		}
		catch (...)
		{
		}
		return *this;
	}

	LogLine& operator<<(const wchar_t* text) noexcept;  // UTF-16 → UTF-8
	LogLine& operator<<(std::wstring_view text) noexcept;
	LogLine& operator<<(wchar_t ch) noexcept;
	LogLine& operator<<(std::ostream& (*manip)(std::ostream&)) noexcept;
	LogLine& operator<<(std::ios_base& (*manip)(std::ios_base&)) noexcept;

private:
	LogLevel _level;
	LogCategory _category;
	std::source_location _location;  // WARN·ERROR 레코드 끝에 "@파일:줄" 로 표기
	LogTimestamp _time;
	std::ostringstream _stream;      // cout 과 같은 숫자 표기를 위해 ostringstream 사용
};

// 매크로 보조: 삼항 연산자 양쪽을 void 로 맞춥니다.
struct LogVoidify
{
	void operator&(const LogLine&) const noexcept {}
};

// 민감 값 표기
namespace LogMask
{
	std::string Ticket(std::string_view ticket);  // "dummy_9401" → "dummy_94...(len=10)"
	std::string Uri(std::string_view uri);        // "tcp://u:pw@host" → "tcp://u:***@host"
}

// 레벨이 꺼져 있으면 << 뒤 인자를 평가하지 않습니다. 끝에 std::endl 을 붙이지 마십시오.
#define MLOG(level, category)                                                                  \
	!::Logger::ShouldLog(level)                                                                \
		? (void)0                                                                              \
		: ::LogVoidify() & ::LogLine(level, LogCategory::category, std::source_location::current())

#define MLOG_TRACE(category) MLOG(LogLevel::Trace, category)
#define MLOG_INFO(category) MLOG(LogLevel::Info, category)
#define MLOG_WARN(category) MLOG(LogLevel::Warn, category)
#define MLOG_ERROR(category) MLOG(LogLevel::Error, category)
