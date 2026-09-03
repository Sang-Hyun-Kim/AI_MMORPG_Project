#include "CorePch.h"
#include "Session.h"
#include "SocketUtils.h"
#include "Service.h"
#include "SendBuffer.h"

/*--------------
	Session
---------------*/

Session::Session() : _recvBuffer(BUFFER_SIZE)
{
	_socket = SocketUtils::CreateSocket();
}

Session::~Session()
{
	SocketUtils::Close(_socket);
}

void Session::UpdateActiveTick()
{
	_lastActiveTick.store(::GetTickCount64());
}

void Session::Send(SendBufferRef sendBuffer)
{
	if (IsConnected() == false)
		return;

	// 락프리 큐에 삽입 (Multi-Producer 지원)
	_sendQueue.Push(sendBuffer);

	// 아직 Send가 등록되지 않았다면 등록한다.
	if (_sendRegistered.exchange(true) == false)
	{
		RegisterSend();
	}
}

bool Session::Connect()
{
	return RegisterConnect();
}

void Session::Disconnect(const WCHAR* cause)
{
	if (_connected.exchange(false) == false)
		return;

	std::wcout << L"Disconnect : " << cause << std::endl;
	RegisterDisconnect();
}

HANDLE Session::GetHandle()
{
	return reinterpret_cast<HANDLE>(_socket);
}

void Session::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
{
	switch (iocpEvent->eventType)
	{
	case EventType::Connect:
		ProcessConnect();
		break;
	case EventType::Disconnect:
		ProcessDisconnect();
		break;
	case EventType::Recv:
		ProcessRecv(numOfBytes);
		break;
	case EventType::Send:
		ProcessSend(numOfBytes);
		break;
	default:
		break;
	}
}

bool Session::RegisterConnect()
{
	if (IsConnected())
		return false;

	if (GetService()->GetServiceType() != ServiceType::Client)
		return false;

	if (SocketUtils::SetReuseAddress(_socket, true) == false)
		return false;

	if (SocketUtils::BindAnyAddress(_socket, 0) == false)
		return false;

	_connectEvent.Init();
	_connectEvent.owner = shared_from_this(); // ADD_REF

	DWORD numOfBytes = 0;
	SOCKADDR_IN sockAddr = GetService()->GetNetAddress().GetSockAddr();
	if (false == SocketUtils::ConnectEx(_socket, reinterpret_cast<SOCKADDR*>(&sockAddr), sizeof(sockAddr), nullptr, 0, &numOfBytes, &_connectEvent))
	{
		int32 errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			_connectEvent.owner = nullptr; // RELEASE_REF
			return false;
		}
	}

	return true;
}

bool Session::RegisterDisconnect()
{
	_disconnectEvent.Init();
	_disconnectEvent.owner = shared_from_this(); // ADD_REF

	if (false == SocketUtils::DisconnectEx(_socket, &_disconnectEvent, TF_REUSE_SOCKET, 0))
	{
		int32 errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			_disconnectEvent.owner = nullptr; // RELEASE_REF
			return false;
		}
	}

	return true;
}

void Session::RegisterRecv()
{
	if (IsConnected() == false)
		return;

	_recvEvent.Init();
	_recvEvent.owner = shared_from_this(); // ADD_REF

	WSABUF wsaBuf;
	wsaBuf.buf = reinterpret_cast<char*>(_recvBuffer.WriteSpan().data());
	wsaBuf.len = static_cast<ULONG>(_recvBuffer.WriteSpan().size());

	DWORD numOfBytes = 0;
	DWORD flags = 0;
	if (SOCKET_ERROR == ::WSARecv(_socket, &wsaBuf, 1, OUT &numOfBytes, OUT &flags, &_recvEvent, nullptr))
	{
		int32 errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			HandleError(errorCode);
			_recvEvent.owner = nullptr; // RELEASE_REF
		}
	}
}

void Session::RegisterSend()
{
	if (IsConnected() == false)
		return;

	_sendEvent.Init();
	_sendEvent.owner = shared_from_this(); // ADD_REF

	SendBufferRef sendBuffer;
	while (_sendQueue.Pop(sendBuffer))
	{
		_sendEvent.sendBuffers.push_back(sendBuffer);
	}

	if (_sendEvent.sendBuffers.empty())
	{
		_sendRegistered.store(false);
		_sendEvent.owner = nullptr;
		return;
	}

	std::vector<WSABUF> wsaBufs;
	wsaBufs.reserve(_sendEvent.sendBuffers.size());
	for (SendBufferRef sb : _sendEvent.sendBuffers)
	{
		WSABUF wsaBuf;
		wsaBuf.buf = reinterpret_cast<char*>(sb->Buffer().data());
		wsaBuf.len = static_cast<ULONG>(sb->WriteSize());
		wsaBufs.push_back(wsaBuf);
	}

	DWORD numOfBytes = 0;
	if (SOCKET_ERROR == ::WSASend(_socket, wsaBufs.data(), static_cast<DWORD>(wsaBufs.size()), OUT &numOfBytes, 0, &_sendEvent, nullptr))
	{
		int32 errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			HandleError(errorCode);
			_sendEvent.owner = nullptr; // RELEASE_REF
			_sendEvent.sendBuffers.clear(); // RELEASE_REF
			_sendRegistered.store(false);
		}
	}
}

void Session::ProcessConnect()
{
	_connectEvent.owner = nullptr; // RELEASE_REF
	_connected.store(true);

	// [N4] 아웃바운드(ClientService/DummyClient) 소켓에도 동일하게 적용
	SocketUtils::SetTcpNoDelay(_socket, true);

	UpdateActiveTick();
	GetService()->AddSession(GetSessionRef());
	OnConnected();
	RegisterRecv();
}

void Session::ProcessDisconnect()
{
	_disconnectEvent.owner = nullptr; // RELEASE_REF
	OnDisconnected();
	GetService()->ReleaseSession(GetSessionRef());
}

void Session::ProcessRecv(int32 numOfBytes)
{
	_recvEvent.owner = nullptr; // RELEASE_REF
	UpdateActiveTick();

	if (numOfBytes == 0)
	{
		Disconnect(L"Recv 0");
		return;
	}

	if (_recvBuffer.OnWrite(numOfBytes) == false)
	{
		Disconnect(L"OnWrite Overflow");
		return;
	}

	std::span<std::byte> readSpan = _recvBuffer.ReadSpan();
	int32 processLen = OnRecv(readSpan); 
	
	if (processLen < 0 || readSpan.size() < processLen || _recvBuffer.OnRead(processLen) == false)
	{
		Disconnect(L"OnRead Overflow");
		return;
	}
	
	_recvBuffer.Clean();
	RegisterRecv();
}

void Session::ProcessSend(int32 numOfBytes)
{
	_sendEvent.owner = nullptr; // RELEASE_REF
	_sendEvent.sendBuffers.clear(); // RELEASE_REF

	if (numOfBytes == 0)
	{
		Disconnect(L"Send 0");
		return;
	}

	OnSend(numOfBytes);

	RegisterSend();
}

void Session::HandleError(int32 errorCode)
{
	switch (errorCode)
	{
	case WSAECONNRESET:
	case WSAECONNABORTED:
		Disconnect(L"HandleError");
		break;
	default:
		std::cout << "Handle Error : " << errorCode << std::endl;
		break;
	}
}

/*-----------------
	PacketSession
------------------*/

PacketSession::PacketSession()
{
}

PacketSession::~PacketSession()
{
}

int32 PacketSession::OnRecv(std::span<std::byte> buffer)
{
	int32 processLen = 0;
	int32 len = static_cast<int32>(buffer.size());

	while (true)
	{
		int32 dataSize = len - processLen;
		if (dataSize < sizeof(PacketHeader))
			break;

		PacketHeader header = *(reinterpret_cast<PacketHeader*>(&buffer[processLen]));

		// ---------------------------------------------------------------------
		// [S1] 하한 검증 — 원격 DoS 차단 (최중대)
		// header.size가 헤더 크기(4)보다 작으면 processLen이 전진하지 않아
		// while(true)가 무한 루프에 빠지고, 이 IOCP 워커 스레드가 영구 점유됩니다.
		// size == 0인 4바이트 패킷 하나로 원격에서 유발 가능하며,
		// 워커가 5개뿐이므로 5회 반복하면 서버 전체가 정지합니다.
		// 리슨 주소가 0.0.0.0으로 바뀌어 외부망에 노출되므로(=[B1]) 반드시 필요합니다.
		// ---------------------------------------------------------------------
		if (header.size < sizeof(PacketHeader))
		{
			Disconnect(L"INVALID_PACKET_SIZE_UNDERFLOW");
			return len; // 남은 버퍼를 전부 소비 처리해 재진입을 막습니다.
		}

		// [S4] 상한 검증 (하드닝) — 상세 사유는 Session.h의 MAX_PACKET_SIZE 주석 참고
		if (header.size > PacketSession::MAX_PACKET_SIZE)
		{
			Disconnect(L"INVALID_PACKET_SIZE_OVERFLOW");
			return len;
		}

		if (dataSize < header.size)
			break;

		OnRecvPacket(buffer.subspan(processLen, header.size));

		processLen += header.size;
	}

	return processLen;
}
