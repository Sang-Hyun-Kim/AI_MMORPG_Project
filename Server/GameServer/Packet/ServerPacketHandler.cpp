#include "ServerPacketHandler.h"
#include <iostream>
#include <charconv>	// [2026-09-04] 티켓 값 안전 파싱(ParseUInt64)
#include "GameSession.h"
#include "GameRoom.h"
#include "RedisManager.h"

std::array<PacketHandlerFunc, UINT16_MAX + 1> GPacketHandler; // [S2] 65536칸

namespace
{
	/*
	 * ParseUInt64
	 * 신뢰할 수 없는 문자열을 uint64로 안전하게 변환합니다.
	 * std::stoull은 실패 시 예외를 던지고 "12abc" 같은 부분 문자열도 통과시키므로,
	 * 문자열 전체가 숫자일 때만 성공으로 판정하는 from_chars를 씁니다.
	 * 반환: 성공 여부. 실패 시 out은 건드리지 않습니다.
	 */
	bool ParseUInt64(const std::string& text, uint64& out)
	{
		if (text.empty())
			return false;

		uint64 value = 0;
		const char* begin = text.data();
		const char* end = text.data() + text.size();

		auto [ptr, ec] = std::from_chars(begin, end, value);
		if (ec != std::errc{} || ptr != end)
			return false;

		out = value;
		return true;
	}

}

bool Handle_INVALID(PacketSessionRef& session, std::span<std::byte> buffer)
{
	return false;
}
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	std::wcout << L"[ServerPacketHandler] C_LOGIN Received! Ticket: " << pkt.ticket().c_str() << std::endl;

	std::string ticket = pkt.ticket();

	if (GRedisManager && GRedisManager->GetRedis())
	{
		std::string key = "Ticket:User:" + ticket;

		// Lua Script 대신 간편하게 Transaction(GET+DEL)을 사용하는 예시
		auto val = GRedisManager->GetRedis()->get(key);
		if (val)
		{
			GRedisManager->GetRedis()->del(key);	// 1회용 소비 — 티켓 재사용 차단

			/*
			 * [2026-09-04] 신원 결속: Redis 값(PlayerId)을 세션에 보관합니다.
			 *
			 * 변경 전: get()으로 읽은 val을 **사용하지 않고 버렸습니다.**
			 *   티켓이 존재하는지만 확인하고 값은 무시했기 때문에, C# 백엔드가
			 *   AccountId를 넣어주고 있었음에도 게임 서버는 "누가 접속했는지"를
			 *   전혀 알지 못했습니다. (C# 백엔드가 데모에서 아무 역할도 못한 이유)
			 * 변경 후: 값을 PlayerId로 파싱해 세션에 보관하고, 이후 C_ENTER_GAME이
			 *   이 값으로 DB에서 해당 캐릭터를 불러옵니다.
			 *
			 * ⚠️ Redis 값은 외부에서 주입될 수 있는 입력으로 취급합니다.
			 *   숫자 파싱에 실패하거나 0이면 신원을 확정할 수 없으므로 즉시 끊습니다.
			 */
			uint64 playerId = 0;
			if (ParseUInt64(*val, playerId) == false || playerId == 0)
			{
				std::wcout << L"Login Failed: Malformed Ticket Payload (ticket=" << ticket.c_str() << L")" << std::endl;
				session->Disconnect(L"Malformed Ticket Payload");
				return false;
			}

			GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
			gameSession->SetPlayerId(playerId);

			// 검증 성공
			Protocol::S_LOGIN loginPkt;
			loginPkt.set_success(true);
			SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(loginPkt);
			session->Send(sendBuffer);
			std::wcout << L"[ServerPacketHandler] Login Success! Ticket: " << ticket.c_str()
					   << L" -> PlayerId " << playerId << std::endl;
			return true;
		}
		else
		{
			// 검증 실패
			std::wcout << L"Login Failed: Invalid Ticket (" << ticket.c_str() << L")" << std::endl;
			session->Disconnect(L"Invalid Ticket");
			return false;
		}
	}
	else
	{
		/*
		 * [2026-09-06] Redis 오프라인 우회를 제거했습니다.
		 *
		 * 변경 전: Redis 가 없으면 티켓 접미사 숫자를 PlayerId 로 삼아 **무조건 승인**했습니다.
		 *   저장소 공개 시 이 규칙이 그대로 노출되므로, 서버가 문자열 패턴을 신뢰하는
		 *   경로를 남겨 둘 수 없습니다.
		 * 변경 후: 티켓을 검증할 수단이 없으면 **로그인을 거부**합니다.
		 *   Redis 는 인증에 필수 구성요소이며, 없으면 신원을 확정할 방법이 없습니다.
		 *
		 * 🚨 이 분기를 "테스트 편의"를 이유로 되살리지 마십시오.
		 *   검증용 티켓은 DummyClient 가 Redis 에 직접 등록합니다(C# 백엔드와 동일 규약).
		 *   서버는 이제 Redis 검증이라는 **단일 경로**만 가집니다.
		 */
		std::wcout << L"Login Failed: Redis unavailable — cannot validate ticket ("
				   << ticket.c_str() << L")" << std::endl;
		session->Disconnect(L"Auth Backend Unavailable");
		return false;
	}
	return false;
}
bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);

	/*
	 * [2026-09-04] 프로세스 카운터 제거 — 결함 F7 1번째 겹
	 *
	 * 변경 전:
	 *     static std::atomic<uint64> idGenerator = 1;
	 *     uint64 tempPlayerId = idGenerator.fetch_add(1);
	 *     gameSession->LoadPlayerTask(1, tempPlayerId);   // accountId는 상수 1
	 *
	 *   playerId가 서버 프로세스의 카운터였습니다. 접속할 때마다 증가하고 서버를
	 *   재시작하면 다시 1부터 시작하므로, 같은 사람이 다시 들어와도 다른 캐릭터가
	 *   되었습니다. "저장 후 복원" 왕복이 성립할 수 없었고, DB의 UPDATE는 존재하지
	 *   않는 행을 겨냥해 0행을 갱신하고 있었습니다(F7 3번째 겹).
	 *
	 * 변경 후:
	 *   신원은 C_LOGIN에서 티켓으로 이미 확정되었습니다. 그 값을 그대로 씁니다.
	 *
	 * 부수 효과:
	 *   과거에는 C_LOGIN 없이 C_ENTER_GAME만 보내도 입장이 되었습니다.
	 *   아래 검사가 그 경로도 함께 막습니다.
	 */
	const uint64 playerId = gameSession->GetPlayerId();
	if (playerId == 0)
	{
		std::wcout << L"[ServerPacketHandler] C_ENTER_GAME rejected: login required." << std::endl;
		session->Disconnect(L"Enter Before Login");
		return false;
	}

	// C_ENTER_GAME을 수신했을 때 비로소 플레이어를 생성/로드하고,
	// S_ENTER_GAME 선발송 후 GameRoom::Enter(S_SPAWN)를 실행!
	gameSession->LoadPlayerTask(playerId);
	return true;
}
bool Handle_C_LEAVE_GAME(PacketSessionRef& session, Protocol::C_LEAVE_GAME& pkt)
{
	return true;
}
bool Handle_C_MOVE(PacketSessionRef& session, Protocol::C_MOVE& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->GetPlayer();
	if (player == nullptr)
		return false;

	GameRoomRef room = player->GetRoom();
	if (room == nullptr)
		return false;

	room->DoAsync(&GameRoom::HandleMove, player, pkt);
	return true;
}
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	return true;
}
bool Handle_C_PING(PacketSessionRef& session, Protocol::C_PING& pkt)
{
	Protocol::S_PONG pongPkt;
	SendBufferRef sendBuffer = ServerPacketHandler::MakeSendBuffer(pongPkt);
	session->Send(sendBuffer);
	return true;
}
bool Handle_C_ATTACK(PacketSessionRef& session, Protocol::C_ATTACK& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->GetPlayer();
	if (player == nullptr)
		return false;

	GameRoomRef room = player->GetRoom();
	if (room == nullptr)
		return false;

	room->DoAsync(&GameRoom::HandleAttack, player, pkt);
	return true;
}
bool Handle_C_CHECK_MAILBOX(PacketSessionRef& session, Protocol::C_CHECK_MAILBOX& pkt)
{
	GameSessionRef gameSession = std::static_pointer_cast<GameSession>(session);
	PlayerRef player = gameSession->GetPlayer();
	if (player == nullptr)
		return false;

	GameRoomRef room = player->GetRoom();
	if (room == nullptr)
		return false;

	room->DoAsync(&GameRoom::HandleCheckMailbox, player, pkt);
	return true;
}