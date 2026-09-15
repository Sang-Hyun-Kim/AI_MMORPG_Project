// [TD-03 · T1-4] RecvBuffer — 읽기/쓰기 위치와 Clean 압축 조건.
//   RecvBuffer(4) 의 용량은 4 x BUFFER_COUNT(10) = 40. Clean 은 데이터가 0 이면 위치를 0 으로,
//   남은 공간(FreeSize)이 bufferSize 보다 작을 때만 남은 데이터를 앞으로 복사함(RecvBuffer.cpp:18-34).
#include <doctest/doctest.h>
#include "RecvBuffer.h"
#include <cstddef>
#include <cstdint>

namespace
{
	// WriteSpan 앞 n 바이트에 start, start+1, ... 을 쓰고 OnWrite(n) — 실제 ProcessRecv 가 커널 복사 후 OnWrite 하는 순서를 흉내
	bool WriteSequence(RecvBuffer& rb, std::int32_t n, std::uint8_t start)
	{
		auto span = rb.WriteSpan();
		if (static_cast<std::int32_t>(span.size()) < n)
			return false;
		for (std::int32_t i = 0; i < n; ++i)
			span[static_cast<std::size_t>(i)] = static_cast<std::byte>(static_cast<std::uint8_t>(start + i));
		return rb.OnWrite(n);
	}
}

TEST_SUITE("RecvBuffer")
{
	TEST_CASE("fresh buffer: capacity is bufferSize x 10")
	{
		RecvBuffer rb(4);
		CHECK(rb.DataSize() == 0);
		CHECK(rb.FreeSize() == 40);
	}

	TEST_CASE("OnWrite rejects more than FreeSize, accepts exactly FreeSize")
	{
		RecvBuffer rb(4);
		CHECK(rb.OnWrite(41) == false);
		CHECK(rb.FreeSize() == 40);     // 거부 시 위치 불변
		CHECK(rb.OnWrite(40) == true);
		CHECK(rb.FreeSize() == 0);
		CHECK(rb.DataSize() == 40);
	}

	TEST_CASE("OnRead rejects more than DataSize")
	{
		RecvBuffer rb(4);
		REQUIRE(WriteSequence(rb, 10, 0));
		CHECK(rb.OnRead(rb.DataSize() + 1) == false);
		CHECK(rb.DataSize() == 10);     // 거부 시 위치 불변
		CHECK(rb.OnRead(10) == true);
		CHECK(rb.DataSize() == 0);
	}

	TEST_CASE("Clean on empty data resets positions")
	{
		RecvBuffer rb(4);
		REQUIRE(WriteSequence(rb, 10, 0));
		REQUIRE(rb.OnRead(10));
		CHECK(rb.FreeSize() == 30);     // Clean 전: 쓰기 위치 10
		rb.Clean();
		CHECK(rb.DataSize() == 0);
		CHECK(rb.FreeSize() == 40);
	}

	TEST_CASE("Clean compacts when FreeSize < bufferSize and keeps unread bytes")
	{
		RecvBuffer rb(4);
		REQUIRE(WriteSequence(rb, 38, 0));   // 바이트 0..37
		REQUIRE(rb.OnRead(30));              // 30..37 이 남음
		REQUIRE(rb.FreeSize() == 2);         // 2 < 4 → 압축 조건
		rb.Clean();
		CHECK(rb.DataSize() == 8);
		CHECK(rb.FreeSize() == 32);
		auto read = rb.ReadSpan();
		REQUIRE(read.size() == 8);
		for (std::size_t i = 0; i < read.size(); ++i)
		{
			CAPTURE(i);
			CHECK(std::to_integer<int>(read[i]) == static_cast<int>(30 + i));
		}
	}

	TEST_CASE("Clean does not compact when FreeSize >= bufferSize")
	{
		RecvBuffer rb(4);
		REQUIRE(WriteSequence(rb, 20, 0));
		REQUIRE(rb.OnRead(10));
		REQUIRE(rb.FreeSize() == 20);        // 20 >= 4 → 복사 안 함
		rb.Clean();
		CHECK(rb.DataSize() == 10);
		CHECK(rb.FreeSize() == 20);          // 쓰기 위치 유지 = 압축하지 않았다는 증거
		CHECK(std::to_integer<int>(rb.ReadSpan()[0]) == 10);
	}
}
