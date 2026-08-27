#include "CorePch.h"
#include "Service.h"
#include "Session.h"
#include "Listener.h"

/*-------------
	Service
--------------*/

Service::Service(ServiceType type, NetAddress address, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount)
	: _type(type), _netAddress(address), _iocpCore(core), _sessionFactory(factory), _maxSessionCount(maxSessionCount)
{

}

Service::~Service()
{
}

void Service::CloseService()
{
	// TODO
}

void Service::Broadcast(SendBufferRef sendBuffer)
{
	std::lock_guard<std::mutex> guard(_lock);
	for (const auto& session : _sessions)
	{
		session->Send(sendBuffer);
	}
}

SessionRef Service::CreateSession()
{
	SessionRef session = _sessionFactory();
	session->SetService(shared_from_this());

	if (_iocpCore->Register(session) == false)
		return nullptr;

	return session;
}

void Service::AddSession(SessionRef session)
{
	std::lock_guard<std::mutex> guard(_lock);
	_sessionCount++;
	_sessions.insert(session);
}

void Service::ReleaseSession(SessionRef session)
{
	std::lock_guard<std::mutex> guard(_lock);
	ASSERT_CRASH(_sessions.erase(session) != 0);
	_sessionCount--;
}

void Service::SweepSessions()
{
	std::lock_guard<std::mutex> guard(_lock);
	uint64_t currentTick = ::GetTickCount64();

	// 복사본을 만들어 순회 중 삭제(Disconnect) 시 Iterator 무효화 방지
	std::vector<SessionRef> zombieSessions;
	
	for (const auto& session : _sessions)
	{
		uint64_t lastTick = session->GetLastActiveTick();
		// 15초(15000ms) 이상 무응답인 세션을 찾는다 (0인 경우는 아직 연결 후 Recv나 Connect가 안된 매우 짧은 순간이거나 버그일 수 있으므로 제외할지 고려. 여기서는 단순화)
		if (lastTick > 0 && currentTick > lastTick + 15000)
		{
			zombieSessions.push_back(session);
		}
	}

	for (const auto& session : zombieSessions)
	{
		session->Disconnect(L"Heartbeat Timeout (Zombie Session)");
	}
}

/*-----------------
	ClientService
------------------*/

ClientService::ClientService(NetAddress targetAddress, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount)
	: Service(ServiceType::Client, targetAddress, core, factory, maxSessionCount)
{
}

bool ClientService::Start()
{
	if (CanStart() == false)
		return false;

	const int32 sessionCount = GetMaxSessionCount();
	for (int32 i = 0; i < sessionCount; i++)
	{
		SessionRef session = CreateSession();
		if (session->Connect() == false)
			return false;
	}

	return true;
}

ServerService::ServerService(NetAddress address, IocpCoreRef core, SessionFactory factory, int32 maxSessionCount)
	: Service(ServiceType::Server, address, core, factory, maxSessionCount)
{
}

bool ServerService::Start()
{
	if (CanStart() == false)
		return false;

	_listener = std::make_shared<Listener>();
	if (_listener == nullptr)
		return false;

	ServerServiceRef service = std::static_pointer_cast<ServerService>(shared_from_this());
	if (_listener->StartAccept(service) == false)
		return false;

	return true;
}

void ServerService::CloseService()
{
	// TODO

	Service::CloseService();
}
