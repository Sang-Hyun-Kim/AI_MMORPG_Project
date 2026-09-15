#pragma once
#include <cmath>
#include <cstdint>

// [TD-03 · D9] AOI 격자 좌표 계산의 정본. GameRoom 의존 없이 단위 시험(ServerTests)에서 직접 검증합니다.
//   GameRoom::SectorCoord / MakeSectorKey 는 선언을 유지하고 이 함수들에 위임합니다.
namespace SectorMath
{
	// [버그 수정] static_cast<int32> 는 0 방향 절단이라 -500 과 +500 이 모두 0 이
	// 됩니다. floor 를 써야 음수 영역에서도 격자 간격이 균일해집니다.
	inline std::int32_t SectorCoord(float v, float cellSize)
	{
		return static_cast<std::int32_t>(std::floor(v / cellSize));
	}

	// 이전의 sx + sy * 1000 은 |sx| 가 500 을 넘으면 다른 셀과 값이 겹칩니다.
	// 상위 32비트에 sx, 하위 32비트에 sy 를 담아 충돌을 없앱니다.
	inline std::int64_t MakeSectorKey(std::int32_t sx, std::int32_t sy)
	{
		return (static_cast<std::int64_t>(sx) << 32) |
		       static_cast<std::int64_t>(static_cast<std::uint32_t>(sy));
	}
}
