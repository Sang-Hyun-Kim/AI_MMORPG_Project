#pragma once
#include "IocpCore.h"
#include "IocpEvent.h"
#include "NetAddress.h"
#include "RecvBuffer.h"
#include "LockFreeQueue.h"

class Service;

/*--------------
	Session
---------------*/

class Session : public IocpObject
{
	friend class Listener;
	friend class IocpCore;
	friend class Service;

	enum
	{
		BUFFER_SIZE = 0x10000, // 64KB
	};

public:
	Session();
	virtual ~Session();

public:
	/* 외부에서 사용 */
	void				Send(SendBufferRef sendBuffer);
	bool				Connect();
	void				Disconnect(const WCHAR* cause);

	std::shared_ptr<Service>	GetService() { return _service.lock(); }
	void				SetService(std::shared_ptr<Service> service) { _service = service; }

public:
	/* 정보 관련 */
	void				SetNetAddress(NetAddress address) { _netAddress = address; }
	NetAddress			GetAddress() { return _netAddress; }
	SOCKET				GetSocket() { return _socket; }
	bool				IsConnected() { return _connected; }
	SessionRef			GetSessionRef() { return std::static_pointer_cast<Session>(shared_from_this()); }

	void				UpdateActiveTick();
	uint64_t			GetLastActiveTick() { return _lastActiveTick.load(); }

private:
	/* 인터페이스 구현 */
	virtual HANDLE		GetHandle() override;
	virtual void		Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) override;

private:
	/* 전송 관련 */
	bool				RegisterConnect();
	bool				RegisterDisconnect();
	void				RegisterRecv();
	void				RegisterSend();

	void				ProcessConnect();
	void				ProcessDisconnect();
	void				ProcessRecv(int32 numOfBytes);
	void				ProcessSend(int32 numOfBytes);

	void				HandleError(int32 errorCode);

protected:
	/* 컨텐츠 코드에서 재정의 */
	virtual void		OnConnected() { }
	virtual int32		OnRecv(std::span<std::byte> buffer) { return static_cast<int32>(buffer.size()); }
	virtual void		OnSend(int32 len) { }
	virtual void		OnDisconnected() { }

private:
	std::weak_ptr<Service>	_service;
	SOCKET				_socket = INVALID_SOCKET;
	NetAddress			_netAddress = {};
	std::atomic<bool>	_connected = false;
	std::atomic<uint64_t> _lastActiveTick = 0;

private:
	/* 수신 관련 */
	RecvBuffer				_recvBuffer;

	/* 송신 관련 */
	LockFreeQueue<SendBufferRef> _sendQueue;
	std::atomic<bool>			_sendRegistered = false;

private:
	/* IocpEvent 재사용 */
	ConnectEvent		_connectEvent;
	DisconnectEvent		_disconnectEvent;
	RecvEvent			_recvEvent;
	SendEvent			_sendEvent;
};

/*-----------------
	PacketSession
------------------*/

#pragma pack(push, 1)
struct PacketHeader
{
	uint16 size;
	uint16 id; // 프로토콜ID
};
#pragma pack(pop)

class PacketSession : public Session
{
public:
	PacketSession();
	virtual ~PacketSession();

	PacketSessionRef	GetPacketSessionRef() { return std::static_pointer_cast<PacketSession>(shared_from_this()); }

protected:
	virtual int32		OnRecv(std::span<std::byte> buffer) final;
	virtual void		OnRecvPacket(std::span<std::byte> buffer) = 0;
};
