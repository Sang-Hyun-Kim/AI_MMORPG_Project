// [TD-03 · T1-5] 수신 프레이밍 — PacketSession::OnRecv(패킷 경계) + RecvBuffer(바이트 위치).
//   Session::ProcessRecv(Session.cpp:223-249) 흐름을 IOCP 없이 재현: 조각을 WriteSpan 에 복사 → OnWrite → OnRecv(ReadSpan)
//   → OnRead(처리 바이트) → Clean. 시험용 세션은 연결된 적이 없어(_connected=false) [S1]·[S4] 거부 시 Disconnect 가 즉시 반환.
//   std::min/max 는 괄호 형태 — ServerCore PCH 의 Windows.h 매크로 회피.
#include <doctest/doctest.h>
#include "Session.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace
{
	constexpr std::int32_t kSessionBufferSize = 0x10000;  // Session::BUFFER_SIZE(private) 와 같은 값 — 실제 세션과 같은 용량(0x10000 x 10 = 655,360 바이트)

	struct PacketSpec
	{
		std::uint16_t id;
		std::uint16_t size;  // 헤더 포함 전체 크기
	};

	// 본문 바이트 규칙: offset i(4 이상) 의 값 = (id * 31 + i) & 0xFF — 경계가 어긋나면 본문 검사가 깨짐
	std::uint8_t BodyByte(std::uint16_t id, std::size_t i)
	{
		return static_cast<std::uint8_t>((id * 31u + i) & 0xFFu);
	}

	void AppendPacket(std::vector<std::byte>& out, PacketSpec p)
	{
		const PacketHeader header{ p.size, p.id };
		const std::size_t base = out.size();
		out.resize(base + (std::max)(static_cast<std::size_t>(p.size), sizeof(PacketHeader)));
		std::memcpy(&out[base], &header, sizeof(PacketHeader));
		for (std::size_t i = sizeof(PacketHeader); i < p.size; ++i)
			out[base + i] = static_cast<std::byte>(BodyByte(p.id, i));
	}

	class TestPacketSession : public PacketSession
	{
	public:
		struct Received
		{
			std::uint16_t id;
			std::uint16_t size;
			bool bodyOk;
		};

		std::int32_t Feed(std::span<std::byte> buffer) { return OnRecv(buffer); }  // protected final 을 시험에서 호출

		std::vector<Received> received;

	protected:
		void OnRecvPacket(std::span<std::byte> buffer) override
		{
			PacketHeader header{};
			std::memcpy(&header, buffer.data(), sizeof(PacketHeader));
			bool ok = (buffer.size() == header.size);
			for (std::size_t i = sizeof(PacketHeader); ok && i < buffer.size(); ++i)
				ok = (std::to_integer<std::uint8_t>(buffer[i]) == BodyByte(header.id, i));
			received.push_back({ header.id, header.size, ok });
		}
	};

	struct DeliverResult
	{
		std::uint64_t consumed = 0;        // OnRecv 가 처리했다고 반환한 바이트 합
		bool writeOverflow = false;        // ProcessRecv 의 "OnWrite Overflow" 에 해당
		bool badProcessLen = false;        // ProcessRecv 의 "OnRead Overflow" 에 해당
		std::int32_t leftover = 0;         // 전달이 끝난 뒤 버퍼에 남은 바이트
	};

	// chunkOf(남은 바이트) 가 이번 조각 크기를 정함
	template <typename ChunkFn>
	DeliverResult Deliver(TestPacketSession& session, const std::vector<std::byte>& stream, ChunkFn chunkOf)
	{
		RecvBuffer rb(kSessionBufferSize);
		DeliverResult r;
		std::size_t pos = 0;
		while (pos < stream.size())
		{
			const std::size_t remaining = stream.size() - pos;
			std::size_t n = (std::min)(chunkOf(remaining), remaining);
			n = (std::min)(n, static_cast<std::size_t>(rb.FreeSize()));
			if (n == 0)  // 남은 공간 0 — 실제 세션이면 다음 수신에서 OnWrite Overflow
			{
				r.writeOverflow = true;
				break;
			}
			std::memcpy(rb.WriteSpan().data(), &stream[pos], n);
			if (rb.OnWrite(static_cast<std::int32_t>(n)) == false)
			{
				r.writeOverflow = true;
				break;
			}
			pos += n;

			auto read = rb.ReadSpan();
			const std::int32_t processLen = session.Feed(read);
			if (processLen < 0 || static_cast<std::int32_t>(read.size()) < processLen || rb.OnRead(processLen) == false)
			{
				r.badProcessLen = true;
				break;
			}
			r.consumed += static_cast<std::uint64_t>(processLen);
			rb.Clean();
		}
		r.leftover = rb.DataSize();
		return r;
	}

	std::vector<std::byte> ThreePackets()
	{
		std::vector<std::byte> s;
		AppendPacket(s, { 1, 4 });    // 헤더만
		AppendPacket(s, { 2, 20 });
		AppendPacket(s, { 3, 300 });
		return s;                     // 324 바이트
	}

	void CheckThreePackets(const TestPacketSession& session, const DeliverResult& r)
	{
		CHECK_FALSE(r.writeOverflow);
		CHECK_FALSE(r.badProcessLen);
		CHECK(r.consumed == 324);
		CHECK(r.leftover == 0);
		REQUIRE(session.received.size() == 3);
		CHECK(session.received[0].id == 1);
		CHECK(session.received[0].size == 4);
		CHECK(session.received[1].id == 2);
		CHECK(session.received[1].size == 20);
		CHECK(session.received[2].id == 3);
		CHECK(session.received[2].size == 300);
		for (const auto& p : session.received)
			CHECK(p.bodyOk);
	}
}

TEST_SUITE("Framing")
{
	TEST_CASE("three packets delivered at once")
	{
		TestPacketSession session;
		const auto r = Deliver(session, ThreePackets(), [](std::size_t remaining) { return remaining; });
		CheckThreePackets(session, r);
	}

	TEST_CASE("three packets delivered one byte at a time")
	{
		TestPacketSession session;
		const auto r = Deliver(session, ThreePackets(), [](std::size_t) { return std::size_t{ 1 }; });
		CheckThreePackets(session, r);
	}

	TEST_CASE("split in the middle of the first header (2 bytes)")
	{
		TestPacketSession session;
		bool first = true;
		const auto r = Deliver(session, ThreePackets(), [&](std::size_t remaining) {
			if (first) { first = false; return std::size_t{ 2 }; }
			return remaining;
		});
		CheckThreePackets(session, r);
	}

	TEST_CASE("random chunking, seeds 1..100")
	{
		int failedSeeds = 0;
		for (std::uint32_t seed = 1; seed <= 100; ++seed)
		{
			CAPTURE(seed);
			std::mt19937 rng(seed);
			std::uniform_int_distribution<std::size_t> dist(1, 64);
			TestPacketSession session;
			const auto r = Deliver(session, ThreePackets(), [&](std::size_t) { return dist(rng); });
			const bool ok = !r.writeOverflow && !r.badProcessLen && r.consumed == 324 && r.leftover == 0
				&& session.received.size() == 3
				&& session.received[0].id == 1 && session.received[1].id == 2 && session.received[2].id == 3
				&& session.received[0].bodyOk && session.received[1].bodyOk && session.received[2].bodyOk;
			CHECK(ok);
			if (!ok)
				++failedSeeds;
		}
		MESSAGE("random chunking seeds=100 failedSeeds=" << failedSeeds);
	}

	TEST_CASE("long stream beyond buffer capacity requires Clean compaction")
	{
		// 324 바이트 x 2,500 = 810,000 바이트 > 용량 655,360 — Clean 이 앞으로 당겨 주지 않으면 writeOverflow 로 끝남
		std::vector<std::byte> stream;
		for (int round = 0; round < 2'500; ++round)
		{
			const auto three = ThreePackets();
			stream.insert(stream.end(), three.begin(), three.end());
		}
		std::mt19937 rng(20260915);
		std::uniform_int_distribution<std::size_t> dist(1, 1'500);
		TestPacketSession session;
		const auto r = Deliver(session, stream, [&](std::size_t) { return dist(rng); });
		CHECK_FALSE(r.writeOverflow);
		CHECK_FALSE(r.badProcessLen);
		CHECK(r.consumed == stream.size());
		CHECK(r.leftover == 0);
		CHECK(session.received.size() == 7'500);
		std::size_t badOrder = 0;
		std::size_t badBody = 0;
		for (std::size_t i = 0; i < session.received.size(); ++i)
		{
			if (session.received[i].id != static_cast<std::uint16_t>(i % 3 + 1))
				++badOrder;
			if (!session.received[i].bodyOk)
				++badBody;
		}
		CHECK(badOrder == 0);
		CHECK(badBody == 0);
		MESSAGE("long stream bytes=" << stream.size() << " capacity=" << kSessionBufferSize * 10
		        << " packets=" << session.received.size());
	}

	TEST_CASE("size == MAX_PACKET_SIZE (0x4000) is accepted")
	{
		std::vector<std::byte> s;
		AppendPacket(s, { 7, static_cast<std::uint16_t>(PacketSession::MAX_PACKET_SIZE) });
		TestPacketSession session;
		const auto r = Deliver(session, s, [](std::size_t) { return std::size_t{ 1'000 }; });
		CHECK_FALSE(r.badProcessLen);
		CHECK(r.consumed == 0x4000);
		REQUIRE(session.received.size() == 1);
		CHECK(session.received[0].size == 0x4000);
		CHECK(session.received[0].bodyOk);
	}

	TEST_CASE("[S1] header.size < 4 consumes everything and delivers nothing")
	{
		std::vector<std::byte> s;
		const PacketHeader bad{ 3, 9 };
		s.resize(sizeof(PacketHeader));
		std::memcpy(s.data(), &bad, sizeof(PacketHeader));
		AppendPacket(s, { 2, 20 });  // 뒤따르는 정상 패킷도 처리되면 안 됨
		TestPacketSession session;
		CHECK(session.GetSocket() != INVALID_SOCKET);  // Session() 의 WSASocket 성공 = TestMain 의 SocketUtils::Init 이 유효
		const std::int32_t processed = session.Feed(std::span<std::byte>(s));
		CHECK(processed == static_cast<std::int32_t>(s.size()));
		CHECK(session.received.empty());
		CHECK_FALSE(session.IsConnected());
	}

	TEST_CASE("[S4] header.size > MAX_PACKET_SIZE consumes everything and delivers nothing")
	{
		std::vector<std::byte> s;
		const PacketHeader bad{ static_cast<std::uint16_t>(PacketSession::MAX_PACKET_SIZE + 1), 9 };
		s.resize(sizeof(PacketHeader));
		std::memcpy(s.data(), &bad, sizeof(PacketHeader));  // 본문이 오기 전, 헤더만으로 거부돼야 함
		TestPacketSession session;
		const std::int32_t processed = session.Feed(std::span<std::byte>(s));
		CHECK(processed == static_cast<std::int32_t>(s.size()));
		CHECK(session.received.empty());
	}
}
