// [TD-03 · T1-6] B3 DBConnectionPool::WorkerThread 의 ConnectionLease — 작업이 던져도 커넥션을 반납하는가.
//   실제 MySQL(Config.json) 필요 · ctest 라벨 db. DB 가 없으면 REQUIRE 실패(건너뛰지 않음).
//   판정: ① 던지는 작업 3회(풀 크기 2 초과) 뒤 작업 2개가 동시에 커넥션을 잡는가(풀 크기 2 유지)
//         ② 로그에 이번 실행 고유 토큰이 든 "[DBConnectionPool] job threw:" 가 정확히 3건
//         ③ 탐지기 검증: 커넥션을 일부러 붙잡으면 확인 작업이 시간 초과되는가
//   반납이 새면 워커가 Pop() 에서 영구 대기 → Clear() join 도 멈춤 → ctest TIMEOUT 이 최종 방어선.
#include <doctest/doctest.h>
#include "ConfigManager.h"
#include "DBConnectionPool.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	namespace fs = std::filesystem;
	using namespace std::chrono_literals;

	constexpr int kPoolSize = 2;

	bool ConnectPool(DBConnectionPool& pool)
	{
		ConfigManager cfg;
		REQUIRE(cfg.Init(MMO_TEST_CONFIG_PATH));  // 자격증명은 로컬 Config.json 에서만 읽고 출력하지 않음
		const DatabaseConfig& db = cfg.databaseConfig;
		return pool.Connect(kPoolSize, db.mySqlHost, db.mySqlPort, db.mySqlUser, db.mySqlPassword, db.mySqlDatabase);
	}

	// 작업 폴더의 Logs/ServerTests_*_p<pid>.log 전부에서 needle 이 든 줄 수(자정 분할 대비 전부 읽음)
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
}

TEST_SUITE("DbPool")
{
	TEST_CASE("B3: throwing jobs return their connections (pool size stays 2)")
	{
		DBConnectionPool pool;
		REQUIRE(ConnectPool(pool));

		// PID 재사용·이전 실행 로그와 섞이지 않게 실행마다 고유 토큰
		const std::string token = "td03-b3-injected-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64());
		for (int i = 0; i < 3; ++i)
			pool.PushJob([token](DBConnection*) { throw std::runtime_error(token); });

		// 확인 작업 2개: 서로를 최대 5초 기다림 → 커넥션 2개가 동시에 대여 가능해야 maxInside == 2
		std::atomic<int> inside{ 0 };
		std::atomic<int> maxInside{ 0 };
		std::promise<void> done1;
		std::promise<void> done2;
		auto probe = [&](std::promise<void>& done) {
			return [&](DBConnection*) {
				const int now = ++inside;
				int prev = maxInside.load();
				while (now > prev && !maxInside.compare_exchange_weak(prev, now)) {}
				const auto deadline = std::chrono::steady_clock::now() + 5s;
				// maxInside 로 기다림 — inside 로 기다리면 먼저 끝난 쪽이 inside 를 줄여 남은 쪽이 5초를 헛기다림
				while (maxInside.load() < kPoolSize && std::chrono::steady_clock::now() < deadline)
					std::this_thread::sleep_for(1ms);
				--inside;
				done.set_value();
			};
		};
		pool.PushJob(probe(done1));
		pool.PushJob(probe(done2));

		CHECK(done1.get_future().wait_for(10s) == std::future_status::ready);
		CHECK(done2.get_future().wait_for(10s) == std::future_status::ready);
		CHECK(maxInside.load() == kPoolSize);

		pool.Clear();
		Logger::Flush();
		const int logged = CountInOwnLogs("[DBConnectionPool] job threw: " + token);
		CHECK(logged == 3);  // 기록이 있어야 통과 — 로그 부재로 판정하지 않음
		MESSAGE("B3 token=" << token << " logged=" << logged << " maxInside=" << maxInside.load());
	}

	TEST_CASE("detector: probe stalls while connections are withheld")
	{
		DBConnectionPool pool;
		REQUIRE(ConnectPool(pool));

		// 반납 누수를 흉내: 시험이 커넥션 2개를 직접 빌려 돌려주지 않음
		DBConnection* a = pool.Pop();
		DBConnection* b = pool.Pop();
		REQUIRE(a != nullptr);
		REQUIRE(b != nullptr);

		std::promise<void> done;
		std::future<void> future = done.get_future();
		pool.PushJob([&](DBConnection*) { done.set_value(); });

		CHECK(future.wait_for(1s) == std::future_status::timeout);  // 커넥션이 없으면 확인 작업이 멈춰야 함
		pool.Push(a);
		pool.Push(b);
		CHECK(future.wait_for(5s) == std::future_status::ready);    // 반납하면 곧바로 진행
		pool.Clear();
	}
}
