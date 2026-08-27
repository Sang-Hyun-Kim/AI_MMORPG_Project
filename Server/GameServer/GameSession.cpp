#include "GameSession.h"
#include "ServerPacketHandler.h"
#include "GameRoomManager.h"

GameSession::GameSession()
{
}

GameSession::~GameSession()
{
}

void GameSession::OnConnected()
{
	std::wcout << L"GameSession Connected" << std::endl;

	// 더미 접속 시 임시로 플레이어 생성 및 로비 진입
	static std::atomic<uint64> idGenerator = 1;

	PlayerRef player = std::make_shared<Player>();
	player->SetObjectId(idGenerator.fetch_add(1));
	player->SetSession(static_pointer_cast<GameSession>(shared_from_this()));

	SetPlayer(player);

	GameRoomRef room = GGameRoomManager->GetRoom(1);
	if (room != nullptr)
	{
		room->DoAsync(&GameRoom::Enter, std::static_pointer_cast<GameObject>(player));
	}
}

void GameSession::OnDisconnected()
{
	std::wcout << L"GameSession Disconnected" << std::endl;

	if (_player)
	{
		GameRoomRef room = _player->GetRoom();
		if (room != nullptr)
		{
			room->DoAsync(&GameRoom::Leave, std::static_pointer_cast<GameObject>(_player));
		}
		_player->SetSession(nullptr);
		_player = nullptr;
	}
}

void GameSession::OnRecvPacket(std::span<std::byte> buffer)
{
	PacketSessionRef session = GetPacketSessionRef();
	ServerPacketHandler::HandlePacket(session, buffer);
}

void GameSession::OnSend(int32 len)
{
	// std::wcout << L"OnSend: " << len << L" bytes" << std::endl;
}
