#include "Service.h"
#include "Session.h"
#include "SocketUtils.h"
#include "ThreadManager.h"
#include <crtdbg.h>

#include "ServerPacketHandler.h"

#include "GameRoomManager.h"
#include "FaultInjection.h"
#include "GameSession.h"
#include "RedisManager.h"

// 서버 실행 상태를 제어하는 전역 플래그
std::atomic<bool> GIsRunning = true;

// [TD-04 K1 · D1 (c)] MySQL 기동 실패 시의 종료 코드. 크래시(0xC…)가 아닌 정상 종료 값이므로
//   운영·시험 스크립트가 "DB 때문에 못 떴다"를 다른 실패와 구분할 수 있습니다.
constexpr int kExitDbUnavailable = 2;

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
    Logger::SetThreadName("CTRL");
    MLOG_INFO(Sys)
        << "\n[System] Shutdown signal received. Preparing graceful shutdown...";
    GIsRunning = false;
    // [TD-01] CLOSE 이벤트는 핸들러 반환 직후 OS가 프로세스를 종료하므로 큐를 비웁니다.
    Logger::Flush();
    return TRUE; // 자체 처리했음을 OS에 알림
  default:
    return FALSE;
  }
}

int main() {
#ifdef _DEBUG
  // [TD-04 K0 · V42] Debug CRT 는 SIGABRT 를 올리기 전에 "abort() has been called" 창을 띄우고 그 스레드를
  //   사람이 버튼을 누를 때까지 멈춥니다 → 무인 Debug 서버가 죽지도 살지도 않는 좀비가 됩니다.
  //   억제하면 PANIC 레코드를 남기고 즉시 종료합니다. JIT 디버거로 붙어야 할 때만 MMO_ABORT_DIALOG=1.
  char abortDialog[8] = {};
  if (::GetEnvironmentVariableA("MMO_ABORT_DIALOG", abortDialog, sizeof(abortDialog)) == 0) {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
  }
#endif

  // [TD-01] 로거를 가장 먼저 켭니다. 설정 로드 실패 로그도 파일에 남기기 위함입니다.
  Logger::SetThreadName("MAIN");
  Logger::Init({ .programName = "GameServer" });

  // 종료 시그널 핸들러 등록
  if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
    MLOG_WARN(Sys) << "[Warning] Could not set control handler";
  }
  // 메모리 누수 탐지 (종료 시 덤프)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

  ServerPacketHandler::Init();
  SocketUtils::Init();
  GGameRoomManager->Init();

  if (!GConfigManager->Init("Config.json")) {
    MLOG_WARN(Config) << "Failed to load Config.json. Using defaults.";
  }

  // [TD-01] Config.json 의 "Log" 섹션 적용 (없으면 기본값 유지)
  Logger::Configure(GConfigManager->logSettings);

  // [TD-02] Debug 전용 실패 주입 설정(환경 변수 MMO_FAULT). Release 에서는 아무것도 하지 않습니다.
  FaultInjection::Init();

  GRedisManager = std::make_shared<RedisManager>();
  if (!GRedisManager->Connect(GConfigManager->databaseConfig.redisString)) {
    MLOG_ERROR(Redis) << "Redis Connect Failed!";
  }

  // [TD-04 K1 · D1 (c)] MySQL 은 기동 필수 자원입니다. 리슨(service->Start()) **전에** 종료하므로
  //   클라이언트가 붙었다가 무응답에 빠지는 창이 아예 없습니다. 변경 전에는 연결 실패에도 서버가 떠서,
  //   입장 요청이 DB 워커(0개)를 기다리며 영구 미재개 → 끊김도 응답도 없는 상태였습니다(부채 DB2).
  //   ⚠️ Redis 는 정책이 다릅니다(위 :78) — 기동 실패에도 뜨고 Handle_C_LOGIN 이 거절합니다.
  //      거절 경로가 있는 자원과 없는 자원의 차이입니다(Code_Specification 18.1).
  if (!GDBConnectionPool->Connect(
          5, // Connection Count
          GConfigManager->databaseConfig.mySqlHost,
          GConfigManager->databaseConfig.mySqlPort,
          GConfigManager->databaseConfig.mySqlUser,
          GConfigManager->databaseConfig.mySqlPassword,
          GConfigManager->databaseConfig.mySqlDatabase,
          GConfigManager->databaseConfig.mySqlReliability)) {
    MLOG_ERROR(Db)
        << "MySQL Connect Failed! MySQL is required at startup. Shutting down.";
    Logger::Flush(); // 비동기 큐의 마지막 ERROR 를 파일에 남기고 나간다
    return kExitDbUnavailable;
  }
  MLOG_INFO(Db) << "MySQL Connected Successfully.";

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
    Logger::SetThreadName("SWEEP");
    while (!stopToken.stop_requested()) {
      service->SweepSessions();
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  });

  for (int32 i = 0; i < 5; i++) {
    GThreadManager->Launch([=](std::stop_token stopToken) {
      Logger::SetThreadName("IOCP", i + 1);
      while (!stopToken.stop_requested()) {
        service->GetIocpCore()->Dispatch(10);
      }
    });
  }

  MLOG_INFO(Sys) << "GameServer is running on port "
                 << GConfigManager->serverConfig.port << "...";

  // 무한 루프 대신 플래그 기반 제어
  while (GIsRunning) {
    GJobTimer->Distribute(::GetTickCount64());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  MLOG_INFO(Sys)
      << "[System] Main loop exited. Broadcasting AutoSave to all rooms...";

  // Phase 3: Graceful Shutdown
  // 모든 접속 중인 유저들의 상태를 캡처하여 DB에 스냅샷 저장(AutoSave)을
  // 명령합니다.
  GGameRoomManager->BroadcastAutoSave();

  // 약간의 딜레이를 주어 GameRoom 워커 스레드들이 DB 워커(DBConnectionPool
  // 큐)로 작업을 넘길 시간을 줍니다.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  MLOG_INFO(Sys) << "[System] Waiting for worker threads and DB flush...";

  // GThreadManager->Join()을 통해 모든 워커 스레드(DB 포함)가 큐에 쌓인 남은
  // 작업을 모두 소진(Flush)할 때까지 대기한 후 안전하게 스레드들을 회수합니다.
  GThreadManager->Join();
  SocketUtils::Clear();
  return 0;
}
