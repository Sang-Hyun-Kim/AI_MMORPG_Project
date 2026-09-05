#pragma once
#include "JobQueue.h"
#include "Player.h"
#include <unordered_set>	// [AOI-1] 셀별 오브젝트 집합

class GameRoom : public JobQueue
{
public:
	GameRoom();
	virtual ~GameRoom();

	void Init();
	void Update();
	void AutoSave();

	void Enter(GameObjectRef gameObject);
	void Leave(GameObjectRef gameObject);

	JobTask SavePlayerToDB(PlayerSaveData data);

	void HandleMove(PlayerRef player, Protocol::C_MOVE pkt);
	
	// Phase 3: 고블린 사냥 및 상점 관련 핸들러
	void HandleAttack(PlayerRef player, Protocol::C_ATTACK pkt);
	void HandleCheckMailbox(PlayerRef player, Protocol::C_CHECK_MAILBOX pkt);
	JobTask ProcessMailboxDB(PlayerRef player); // DB에서 우편함 비동기 조회 및 수령

	void Broadcast(SendBufferRef sendBuffer);

public:
	/*
	 * [AOI-1 / 2026-09-06] AOI 관측 통계
	 *   이동 1회당 "거리 비교를 몇 번 했는가"를 셉니다.
	 *   그리드 도입 전(전체 순회)이었다면 몇 번이었을지도 함께 세어,
	 *   측정 장비 없이 개선폭을 수치로 낼 수 있게 합니다.
	 */
	struct AoiStats
	{
		uint64 queryCount = 0;			// GetAdjacentSectorPlayers 호출 횟수
		uint64 compareCount = 0;		// 실제 수행한 거리 비교 횟수 (그리드 적용 후)
		uint64 naiveCompareCount = 0;	// 같은 상황에서 전체 순회였다면 했을 비교 횟수
		uint64 cellVisitCount = 0;		// 방문한 셀 개수 (빈 셀 포함)
	};
	const AoiStats& GetAoiStats() const { return _aoiStats; }
	void PrintAoiStats() const;

private:
	/*
	 * ─── Uniform Grid 기반 AOI ────────────────────────────────────────────────
	 *
	 * [왜 바꿨나]
	 *   이전 GetAdjacentSectorPlayers 는 이름과 달리 **_players 전체를 순회**하며
	 *   거리를 재는 O(N) 구현이었습니다. 인원이 늘면 이동 1회마다 N번,
	 *   전원이 움직이면 틱당 N^2 번의 거리 비교가 발생합니다.
	 *
	 * [무엇이 바뀌나]
	 *   좌표를 kCellSize 격자로 나눠 셀별 오브젝트 집합을 유지하고,
	 *   질의 시 **반경이 걸치는 셀만** 훑습니다. 셀로 후보를 좁힌 뒤
	 *   **정확한 거리 검사는 그대로 수행**하므로 **반환 결과는 이전과 완전히 동일**합니다.
	 *   즉 이것은 동작 변경이 아니라 **가속 구조**입니다.
	 *
	 * 🚨 [유지 지침] _sectors 는 _players 와 반드시 함께 갱신되어야 합니다.
	 *   Enter / Leave / HandleMove 세 곳이 전부입니다. 한 곳이라도 빠뜨리면
	 *   플레이어가 조용히 사라지거나 유령이 남습니다.
	 *   위치를 바꾸는 코드를 새로 추가한다면 MoveSector 를 반드시 함께 부르십시오.
	 */
	static constexpr float kCellSize   = 1000.0f;	// 격자 한 칸의 크기
	static constexpr float kAoiRadius  = 2000.0f;	// 관측 반경 (기존 값 유지)

	// 반경이 걸치는 셀 범위. 2000 / 1000 = 2 → 중심 기준 ±2, 즉 5x5 = 25칸
	static constexpr int32 kCellSpan   = static_cast<int32>(kAoiRadius / kCellSize);

	/*
	 * [버그 수정] 이전 구현은 static_cast<int32>(x / 1000.f) 였습니다.
	 *   C++ 의 정수 변환은 **0 방향으로 절단**하므로 -500 과 +500 이 모두 0 이 되어
	 *   음수 좌표 영역에서 격자가 깨집니다. 실제 DB 에 저장된 좌표에 음수가 있습니다
	 *   (예: PosX = -603.23). floor 로 바꿔 음수에서도 균일한 격자가 되게 했습니다.
	 */
	static int32 SectorCoord(float v);

	// (sx, sy) 를 하나의 키로 합칩니다. 이전의 sx + sy*1000 은 좌표가 크면 충돌합니다.
	static int64 MakeSectorKey(int32 sx, int32 sy);

	// 이전 시그니처(int32)는 위 절단 문제로 안전하지 않아 int64 로 교체했습니다.
	int64 GetSectorIndex(float x, float y);

	void AddToSector(uint64 objectId, float x, float y);
	void RemoveFromSector(uint64 objectId, float x, float y);
	void MoveSector(uint64 objectId, float oldX, float oldY, float newX, float newY);

	std::vector<PlayerRef> GetAdjacentSectorPlayers(float x, float y);
	void BroadcastToAdjacentSectors(float x, float y, SendBufferRef sendBuffer);

private:
	std::unordered_map<uint64, PlayerRef> _players;

	// 셀 키 → 그 셀에 있는 오브젝트 ID 집합
	std::unordered_map<int64, std::unordered_set<uint64>> _sectors;

	AoiStats _aoiStats;
	// _monsters
};

using GameRoomRef = std::shared_ptr<GameRoom>;
