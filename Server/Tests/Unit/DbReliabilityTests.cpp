// [TD-04 K2] 대여 시 건강 검사·재연결(DB1) 시험. 실제 MySQL(Config.json) 필요 · ctest 라벨 db.
//   Docker 를 멈추지 않고 "서버가 끊은 커넥션"을 만든다 — 별도 관리 커넥션의 KILL, 그리고 wait_timeout.
//   그래서 컨테이너 조작 없이 매 실행 같은 결과가 나온다(결정적).
//   판정은 "있어야 할 로그 레코드가 있는가"로 하며, 같은 프로세스의 로그가 누적되므로
//   케이스마다 **시작 시점 기준선과의 증분**을 센다(절대값을 쓰면 앞 케이스의 기록이 섞인다).
#include <doctest/doctest.h>
#include "ConfigManager.h"
#include "DBConnectionPool.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>

namespace
{
	namespace fs = std::filesystem;
	using namespace std::chrono_literals;

	// 로그 문구 — 제품 코드(DBConnectionPool::EnsureHealthy)와 한 글자라도 달라지면 이 시험이 알린다
	constexpr const char* kPingFailed = "[DBConnectionPool] connection dead (ping failed)";
	constexpr const char* kReconnected = "[DBConnectionPool] reconnected conn=";
	constexpr const char* kStartupFailed = "[DBConnectionPool] startup connect failed at";

	// 작업 폴더의 Logs/ServerTests_*_p<pid>.log 전부에서 needle 이 든 줄 수.
	// (TD-03 DbPoolTests.cpp 의 같은 헬퍼 — 시험 TU 마다 익명 네임스페이스로 둔다)
	int CountInOwnLogs(const std::string& needle)
	{
		const std::string suffix = "_p" + std::to_string(::GetCurrentProcessId()) + ".log";
		int count = 0;
		std::error_code ec;
		for (const fs::directory_entry& entry : fs::directory_iterator("Logs", ec))
		{
			const std::string name = entry.path().filename().string();
			if (name.rfind("ServerTests_", 0) != 0 || name.size() < suffix.size() ||
				name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
				continue;
			std::ifstream in(entry.path(), std::ios::binary);
			std::string line;
			while (std::getline(in, line))
			{
				if (line.find(needle) != std::string::npos)
					++count;
			}
		}
		return count;
	}

	// 케이스 시작 시점의 기록 수를 찍어 두고, 끝에서 증분만 본다
	struct LogBaseline
	{
		LogBaseline()
		{
			Logger::Flush();
			pingFailed = CountInOwnLogs(kPingFailed);
			reconnected = CountInOwnLogs(kReconnected);
			startupFailed = CountInOwnLogs(kStartupFailed);
		}
		int pingFailed = 0;
		int reconnected = 0;
		int startupFailed = 0;
	};

	DatabaseConfig LoadDbConfig()
	{
		ConfigManager cfg;
		REQUIRE(cfg.Init(MMO_TEST_CONFIG_PATH)); // 자격증명은 로컬 Config.json 에서만 읽고 출력하지 않는다
		return cfg.databaseConfig;
	}

	bool ConnectPool(DBConnectionPool& pool, int32 count, const DbReliabilityOptions& options)
	{
		const DatabaseConfig db = LoadDbConfig();
		return pool.Connect(count, db.mySqlHost, db.mySqlPort, db.mySqlUser, db.mySqlPassword, db.mySqlDatabase, options);
	}

	// 항상 ping 하는 설정(시험 전용). 운영 기본값은 30초다.
	DbReliabilityOptions AlwaysPing()
	{
		DbReliabilityOptions options;
		options.pingIdleMs = 0;
		return options;
	}

	// 작업 1건을 넣고 결과를 기다린다. 무응답(코루틴 영구 미재개와 같은 형태)이면 여기서 드러난다.
	template <typename T, typename Fn>
	bool RunJob(DBConnectionPool& pool, T& out, Fn&& body, std::chrono::seconds timeout = 10s)
	{
		std::promise<T> promise;
		std::future<T> future = promise.get_future();
		pool.PushJob([&promise, body](DBConnection* conn) { promise.set_value(body(conn)); });
		if (future.wait_for(timeout) != std::future_status::ready)
			return false;
		out = future.get();
		return true;
	}
}

TEST_SUITE("DbReliability")
{
	TEST_CASE("reconnect: server-side KILL is detected and healed")
	{
		LogBaseline base;
		DBConnectionPool pool;
		REQUIRE(ConnectPool(pool, 1, AlwaysPing()));

		// ① 풀의 유일한 커넥션이 서버에서 어떤 세션인지 확인
		unsigned long threadId = 0;
		REQUIRE(RunJob(pool, threadId, [](DBConnection* conn) { return mysql_thread_id(conn->GetRawConnection()); }));
		REQUIRE(threadId != 0);

		// ② 별도 관리 커넥션에서 그 세션을 끊는다 — 컨테이너를 멈추지 않고 "서버가 닫은 커넥션"을 만든다
		const DatabaseConfig db = LoadDbConfig();
		DBConnection admin;
		REQUIRE(admin.Connect(db.mySqlHost, db.mySqlPort, db.mySqlUser, db.mySqlPassword, db.mySqlDatabase));
		REQUIRE(admin.Execute("KILL " + std::to_string(threadId)));
		std::this_thread::sleep_for(300ms); // 서버가 세션을 정리할 시간

		// ③ 다음 작업 — 감지·재연결 후 정상 쿼리로 끝나야 한다
		bool queryOk = false;
		REQUIRE(RunJob(pool, queryOk, [](DBConnection* conn) {
			DBResult result = conn->ExecuteQuery("SELECT 1");
			return result.HasRow();
		}));
		CHECK(queryOk);

		// ④ 풀 크기 불변(I1): 커넥션이 폐기되지 않았으므로 다음 작업도 실행된다
		bool stillUsable = false;
		CHECK(RunJob(pool, stillUsable, [](DBConnection* conn) { return conn->ExecuteQuery("SELECT 1").HasRow(); }));
		CHECK(stillUsable);

		pool.Clear();
		Logger::Flush();
		const int pingFailed = CountInOwnLogs(kPingFailed) - base.pingFailed;
		const int reconnected = CountInOwnLogs(kReconnected) - base.reconnected;
		CHECK(pingFailed >= 1);   // 기록이 있어야 통과 — 로그 부재로 정상 판정하지 않는다
		CHECK(reconnected >= 1);
		MESSAGE("KILL threadId=" << threadId << " pingFailed=" << pingFailed << " reconnected=" << reconnected);
	}

	TEST_CASE("reconnect: idle timeout (wait_timeout) is healed")
	{
		LogBaseline base;
		DBConnectionPool pool;
		REQUIRE(ConnectPool(pool, 1, AlwaysPing()));

		// 8시간 기본값을 2초로 줄여 유휴 끊김을 축소 재현한다.
		// 시간에 의존하지만 단정 대상은 경과 시간이 아니라 "끊김이 감지되고 복구되었는가"다.
		bool setOk = false;
		REQUIRE(RunJob(pool, setOk, [](DBConnection* conn) { return conn->Execute("SET SESSION wait_timeout=2"); }));
		CHECK(setOk);

		std::this_thread::sleep_for(3500ms); // 서버가 유휴 세션을 닫는다

		bool queryOk = false;
		REQUIRE(RunJob(pool, queryOk, [](DBConnection* conn) { return conn->ExecuteQuery("SELECT 1").HasRow(); }));
		CHECK(queryOk);

		pool.Clear();
		Logger::Flush();
		const int pingFailed = CountInOwnLogs(kPingFailed) - base.pingFailed;
		const int reconnected = CountInOwnLogs(kReconnected) - base.reconnected;
		CHECK(pingFailed >= 1);
		CHECK(reconnected >= 1);
		MESSAGE("wait_timeout pingFailed=" << pingFailed << " reconnected=" << reconnected);
	}

	TEST_CASE("pool: partial connect failure leaves nothing behind")
	{
		LogBaseline base;
		const DatabaseConfig db = LoadDbConfig();

		DBConnectionPool pool;
		// 잘못된 비밀번호 → 첫 커넥션부터 실패. [TD-04 K1] 계약: false 를 반환하면 자원 0
		CHECK_FALSE(pool.Connect(2, db.mySqlHost, db.mySqlPort, db.mySqlUser,
			db.mySqlPassword + "-wrong-on-purpose", db.mySqlDatabase, DbReliabilityOptions{}));

		// 워커가 0개여야 Clear() 가 join 없이 즉시 끝난다(반쪽 상태면 여기서 매달린다)
		const auto start = std::chrono::steady_clock::now();
		pool.Clear();
		const auto elapsed = std::chrono::steady_clock::now() - start;
		CHECK(elapsed < 2s);

		Logger::Flush();
		CHECK(CountInOwnLogs(kStartupFailed) - base.startupFailed == 1); // 실패 위치가 정확히 1건 기록
	}

	TEST_CASE("health: recently used connection skips ping")
	{
		LogBaseline base;
		DBConnectionPool pool;
		REQUIRE(ConnectPool(pool, 1, DbReliabilityOptions{})); // 기본 pingIdleMs=30000

		for (int i = 0; i < 2; ++i)
		{
			bool ok = false;
			REQUIRE(RunJob(pool, ok, [](DBConnection* conn) { return conn->ExecuteQuery("SELECT 1").HasRow(); }));
			CHECK(ok);
		}

		pool.Clear();
		Logger::Flush();
		// 방금 쓴 커넥션은 왕복을 생략하므로 상태 전이 기록이 생길 이유가 없다
		CHECK(CountInOwnLogs(kPingFailed) - base.pingFailed == 0);
		CHECK(CountInOwnLogs(kReconnected) - base.reconnected == 0);
	}
}
