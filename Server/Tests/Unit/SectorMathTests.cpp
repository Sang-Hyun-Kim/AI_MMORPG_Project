// [TD-03 · T1-1 · T1-2] SectorMath — AOI 격자 좌표와 섹터 키.
//   cellSize 1000 은 GameRoom::kCellSize(GameRoom.h:67) 와 같은 값. GameRoom.h 는 GameServer 의존 때문에 포함하지 않음.
//   단정하지 않는 입력: NaN · ±Inf · |v| ≳ 2.147e12 (int32 변환 UB — 입력 검증은 V6 · TD-12).
#include <doctest/doctest.h>
#include "SectorMath.h"
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace
{
	constexpr float kCell = 1000.0f;

	struct CoordCase
	{
		float v;
		std::int32_t expected;
	};
}

TEST_SUITE("SectorMath")
{
	TEST_CASE("SectorCoord: floor grid on both signs")
	{
		const CoordCase cases[] = {
			{ 0.0f, 0 },
			{ -0.0f, 0 },
			{ -0.001f, -1 },        // 0 방향 절단이면 0
			{ -500.0f, -1 },
			{ 500.0f, 0 },          // -500 / 500 이 같은 칸이던 이전 결함의 대표 쌍
			{ 999.999f, 0 },
			{ 1000.0f, 1 },
			{ -1000.0f, -1 },       // 경계는 오른쪽 칸에 속함
			{ -1000.5f, -2 },
			{ -603.23f, -1 },       // 실제 DB 좌표 (GameRoom.h:77)
			{ 2'000'000.0f, 2000 },
			{ -2'000'000.0f, -2000 },
		};

		for (const CoordCase& c : cases)
		{
			CAPTURE(c.v);
			CHECK(SectorMath::SectorCoord(c.v, kCell) == c.expected);
		}
	}

	TEST_CASE("MakeSectorKey: inverse over int32 extremes")
	{
		// (…::min)() — ServerCore PCH 의 Windows.h min/max 매크로 전개를 괄호로 막음
		const std::int32_t values[] = {
			(std::numeric_limits<std::int32_t>::min)(), -1, 0, 1, (std::numeric_limits<std::int32_t>::max)()
		};

		for (std::int32_t sx : values)
		{
			for (std::int32_t sy : values)
			{
				CAPTURE(sx);
				CAPTURE(sy);
				const std::int64_t key = SectorMath::MakeSectorKey(sx, sy);
				CHECK(static_cast<std::int32_t>(key >> 32) == sx);
				CHECK(static_cast<std::int32_t>(static_cast<std::uint32_t>(key)) == sy);
			}
		}
	}

	TEST_CASE("MakeSectorKey: no collision in [-600, 600]^2, legacy formula collides")
	{
		constexpr std::int32_t kMin = -600;
		constexpr std::int32_t kMax = 600;
		constexpr std::size_t kCount = static_cast<std::size_t>(kMax - kMin + 1) * static_cast<std::size_t>(kMax - kMin + 1);
		REQUIRE(kCount == 1'442'401);

		std::unordered_set<std::int64_t> keys;
		std::unordered_set<std::int64_t> legacy;
		keys.reserve(kCount);
		legacy.reserve(kCount);

		for (std::int32_t sx = kMin; sx <= kMax; ++sx)
		{
			for (std::int32_t sy = kMin; sy <= kMax; ++sy)
			{
				keys.insert(SectorMath::MakeSectorKey(sx, sy));
				legacy.insert(static_cast<std::int64_t>(sx) + static_cast<std::int64_t>(sy) * 1000);  // 이전 식 (GameRoom.h:81)
			}
		}

		CHECK(keys.size() == kCount);
		CHECK(legacy.size() < kCount);  // 대조군: 이전 식은 충돌이 있어야 함
		MESSAGE("legacy sx + sy*1000 distinct keys: " << legacy.size() << " / " << kCount);
	}

	TEST_CASE("MakeSectorKey: sign bits do not alias")
	{
		const std::int64_t a = SectorMath::MakeSectorKey(-1, 0);
		const std::int64_t b = SectorMath::MakeSectorKey(0, -1);
		CHECK(a != b);
		CHECK(b == 0x00000000FFFFFFFFLL);
	}
}
