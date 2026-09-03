#include "Service.h"
#include "Session.h"
#include "SocketUtils.h"
#include "ThreadManager.h"
#include <crtdbg.h>

#include "ServerPacketHandler.h"

#include "GameRoomManager.h"
#include "GameSession.h"
#include "RedisManager.h"

// 서버 실행 상태를 제어하는 전역 플래그
std::atomic<bool> GIsRunning = true;

/*
 * ConsoleCtrlHandler
 * 역할: 콘솔 창 닫기(X버튼)나 Ctrl+C(SIGINT) 등 종료 시그널을 가로채는 핸들러
 * 데이터 흐름: 종료 시그널 발생 -> GIsRunning 플래그 false로 변경 -> 메인 틱
 * 루프 탈출 -> DB 강제 플러시 -> 안전 종료
 */
BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
  switch (dwCtrlType) {
  case CTRL_C_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_BREAK_EVENT:
    std::cout
        << "\n[System] Shutdown signal received. Preparing graceful shutdown..."
        << std::endl;
    GIsRunning = false;
    return TRUE; // 자체 처리했음을 OS에 알림
  default:
    return FALSE;
  }
}

int main() {
  // 종료 시그널 핸들러 등록
  if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
    std::cout << "[Warning] Could not set control handler" << std::endl;
  }
  // 메모리 누수 탐지 (종료 시 덤프)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

  ServerPacketHandler::Init();
  SocketUtils::Init();
  GGameRoomManager->Init();

  if (!GConfigManager->Init("Config.json")) {
    std::cout << "Failed to load Config.json. Using defaults." << std::endl;
  }

  GRedisManager = std::make_shared<RedisManager>();
  if (!GRedisManager->Connect(GConfigManager->databaseConfig.redisString)) {
    std::cout << "Redis Connect Failed!" << std::endl;
  }

  if (!GDBConnectionPool->Connect(
          5, // Connection Count
          GConfigManager->databaseConfig.mySqlHost,
          GConfigManager->databaseConfig.mySqlPort,
          GConfigManager->databaseConfig.mySqlUser,
          GConfigManager->databaseConfig.mySqlPassword,
          GConfigManager->databaseConfig.mySqlDatabase)) {
    std::cout << "MySQL Connect Failed!" << std::endl;
  } else {
    std::cout << "MySQL Connected Successfully." << std::endl;
  }

  // [B1] 리슨 주소를 Config.json의 Server.BindAddress에서 읽습니다.
  // 과거에는 L"127.0.0.1"이 하드코딩되어 있어, AWS EC2에 배포해도 루프백에만
  // 바인딩되므로 보안 그룹과 Elastic IP를 열어도 외부 접속이 성립하지 않았습니다.
  const std::string& bindAddr = GConfigManager->serverConfig.bindAddress;
  const std::wstring bindAddrW(bindAddr.begin(), bindAddr.end());

  ServerServiceRef service = std::make_shared<ServerService>(
      NetAddress(bindAddrW, GConfigManager->serverConfig.port),
      std::make_shared<IocpCore>(),
      []() { return std::make_shared<GameSession>(); }, 100);

  ASSERT_CRASH(service->Start());

  // Session Sweeper Thread (1초 주기, C++20 jthread + stop_token)
  GThreadManager->Launch([service](std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
      service->SweepSessions();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  for (int32 i = 0; i < 5; i++) {
    GThreadManager->Launch([=](std::stop_token stopToken) {
      while (!stopToken.stop_requested()) {
        service->GetIocpCore()->Dispatch(10);
      }
    });
  }

  std::cout << "GameServer is running on port "
            << GConfigManager->serverConfig.port << "..." << std::endl;

  // 무한 루프 대신 플래그 기반 제어
  while (GIsRunning) {
    GJobTimer->Distribute(::GetTickCount64());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout
      << "[System] Main loop exited. Broadcasting AutoSave to all rooms..."
      << std::endl;

  // Phase 3: Graceful Shutdown
  // 모든 접속 중인 유저들의 상태를 캡처하여 DB에 스냅샷 저장(AutoSave)을
  // 명령합니다.
  GGameRoomManager->BroadcastAutoSave();

  // 약간의 딜레이를 주어 GameRoom 워커 스레드들이 DB 워커(DBConnectionPool
  // 큐)로 작업을 넘길 시간을 줍니다.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::cout << "[System] Waiting for worker threads and DB flush..."
            << std::endl;

  // GThreadManager->Join()을 통해 모든 워커 스레드(DB 포함)가 큐에 쌓인 남은
  // 작업을 모두 소진(Flush)할 때까지 대기한 후 안전하게 스레드들을 회수합니다.
  GThreadManager->Join();
  SocketUtils::Clear();
  return 0;
}
