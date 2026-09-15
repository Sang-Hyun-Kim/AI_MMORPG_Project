// [TD-03 · T1-3] LockFreeQueue — MPSC(다중 생산자 · 단일 소비자) 무손실 · 순서 보존.
//   소비자 스레드는 doctest 단정을 호출하지 않고 위반을 세기만 함 → 판정은 메인 스레드에서.
//   Pop 의 false 는 "비었음"이 아님(LockFreeQueue.h:73-76 exchange 후 store 전 찰나) → 생산자 join 후 남은 항목을 소진한 뒤 판정.
//   시간 기반 단정 없음. 간헐 실패가 나오면 시험을 완화하지 말고 결함 후보로 등록할 것.
#include <doctest/doctest.h>
#include "LockFreeQueue.h"
#include <atomic>
#include <cstdint>
#include <latch>
#include <thread>
#include <vector>

namespace
{
	struct MpscResult
	{
		std::uint64_t total = 0;
		std::uint64_t orderViolations = 0;    // 생산자별 seq 가 1씩 증가하지 않음(중복·유실·역순)
		std::uint64_t unknownProducer = 0;
		std::vector<std::uint64_t> nextSeq;   // 생산자별 다음 기대 seq = 받은 개수
	};

	// 값 = (producerId << 32) | seq
	MpscResult RunMpsc(std::uint32_t producers, std::uint32_t perProducer)
	{
		LockFreeQueue<std::uint64_t> queue;
		MpscResult result;
		result.nextSeq.assign(producers, 0);

		std::latch start(1);
		std::atomic<bool> producersDone{ false };

		auto consume = [&](std::uint64_t value)
		{
			const std::uint32_t id = static_cast<std::uint32_t>(value >> 32);
			const std::uint32_t seq = static_cast<std::uint32_t>(value);
			++result.total;
			if (id >= producers)
			{
				++result.unknownProducer;
				return;
			}
			if (seq != result.nextSeq[id])
				++result.orderViolations;
			result.nextSeq[id] = static_cast<std::uint64_t>(seq) + 1;
		};

		std::thread consumer([&]
		{
			start.wait();
			std::uint64_t value = 0;
			for (;;)
			{
				if (queue.Pop(value))
				{
					consume(value);
					continue;
				}
				if (producersDone.load(std::memory_order_acquire))
				{
					while (queue.Pop(value))  // 모든 Push 가 끝난 뒤 남은 항목 소진
						consume(value);
					break;
				}
				std::this_thread::yield();
			}
		});

		std::vector<std::thread> threads;
		threads.reserve(producers);
		for (std::uint32_t id = 0; id < producers; ++id)
		{
			threads.emplace_back([&, id]
			{
				start.wait();
				for (std::uint32_t seq = 0; seq < perProducer; ++seq)
					queue.Push((static_cast<std::uint64_t>(id) << 32) | seq);
			});
		}

		start.count_down();  // 생산자·소비자 동시 출발
		for (std::thread& t : threads)
			t.join();
		producersDone.store(true, std::memory_order_release);
		consumer.join();
		return result;
	}

	void CheckMpsc(std::uint32_t producers, std::uint32_t perProducer, int repeats)
	{
		const std::uint64_t expected = static_cast<std::uint64_t>(producers) * perProducer;
		int failedRuns = 0;
		for (int run = 1; run <= repeats; ++run)
		{
			CAPTURE(run);
			const MpscResult r = RunMpsc(producers, perProducer);
			bool ok = (r.total == expected) && (r.orderViolations == 0) && (r.unknownProducer == 0);
			CHECK(r.total == expected);
			CHECK(r.orderViolations == 0);
			CHECK(r.unknownProducer == 0);
			for (std::uint32_t id = 0; id < producers; ++id)
			{
				CAPTURE(id);
				CHECK(r.nextSeq[id] == perProducer);
				ok = ok && (r.nextSeq[id] == perProducer);
			}
			if (!ok)
				++failedRuns;
		}
		MESSAGE("producers=" << producers << " perProducer=" << perProducer << " items/run=" << expected
		        << " runs=" << repeats << " failedRuns=" << failedRuns);
	}
}

TEST_SUITE("LockFreeQueue")
{
	TEST_CASE("MPSC 4 producers x 2,500 = 10,000 items, 20 runs")
	{
		CheckMpsc(4, 2'500, 20);
	}

	TEST_CASE("MPSC stress 8 producers x 10,000 = 80,000 items, 20 runs")
	{
		CheckMpsc(8, 10'000, 20);
	}

	TEST_CASE("destroy with 100 pending items")
	{
		{
			LockFreeQueue<std::uint64_t> queue;
			for (std::uint64_t i = 0; i < 100; ++i)
				queue.Push(i);
		}  // 소멸자가 남은 항목을 Pop 하고 더미 노드를 해제 — 크래시 없음만 확인(누수는 단정하지 않음)
		CHECK(true);
	}
}
