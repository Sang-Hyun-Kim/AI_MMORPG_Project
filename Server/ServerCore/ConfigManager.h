#pragma once
#include <string>
#include <memory>

#include "Logger.h" // [TD-01] LogSettings

// 서버 설정 정보 (포트 번호, 최대 접속 세션 수 등)
struct ServerConfig
{
    // 리슨(bind)할 로컬 인터페이스 주소.
    // "0.0.0.0"(INADDR_ANY)이면 모든 NIC에서 수신하므로 외부망(AWS EC2 Elastic IP) 접속이 가능합니다.
    // "127.0.0.1"로 두면 루프백에만 바인딩되어 같은 PC에서만 접속됩니다.
    std::string bindAddress = "0.0.0.0";
    int32 port = 7777;          // 게임 서버가 리슨할 포트
    int32 maxSession = 1000;    // 최대 동시 접속 허용 세션 수
};

/*
 * DbReliabilityOptions
 * 역할: MySQL 커넥션의 타임아웃·건강 검사·재연결 정책값 묶음입니다. [TD-04 K1]
 * ⚠️ 이 구조체의 초기값이 **배포 기본값의 정본**입니다. Config.json 은 .gitignore 로 로컬 전용이라
 *    새 환경에는 파일이 없고, 그때도 서버가 같은 정책으로 동작해야 합니다. 기본값을 다른 곳에
 *    복제하지 마십시오(ConfigManager 파싱은 "키가 없으면 현재 값 유지" 방식입니다).
 */
struct DbReliabilityOptions
{
    int32 connectTimeoutSec = 3;      // MYSQL_OPT_CONNECT_TIMEOUT — 기동 Fail-Fast 를 빠르게
    int32 readTimeoutSec = 10;        // MYSQL_OPT_READ_TIMEOUT — 죽은 소켓에 워커가 매달리는 것 방지
    int32 writeTimeoutSec = 10;       // MYSQL_OPT_WRITE_TIMEOUT
    uint64 pingIdleMs = 30000;        // [K2] 이보다 오래 쉰 커넥션만 대여 시 ping (0 = 항상)
    int32 reconnectAttempts = 2;      // [K2] 대여 1회당 재연결 시도 상한
    int32 reconnectBackoffMs = 200;   // [K2] 시도 간 대기
};

// 데이터베이스(MySQL, Redis) 접속 문자열 및 설정 정보
struct DatabaseConfig
{
    // MySQL 접속 정보 (하드코딩 방지)
    std::string mySqlHost;
    int32 mySqlPort = 3306;
    std::string mySqlUser;
    std::string mySqlPassword;
    std::string mySqlDatabase;

    // [TD-04 K1] MySQL 신뢰성 정책. Database.MySQL 섹션의 키 6개로 덮어쓸 수 있습니다.
    DbReliabilityOptions mySqlReliability;

    // Redis 접속 정보 (예: tcp://127.0.0.1:6379)
    std::string redisString;
};

/*
 * ConfigManager
 * 역할: 게임 서버 기동 시 'Config.json' 파일을 읽어와 서버 전반의 설정을 들고 있는 싱글톤 클래스.
 * 흐름: 
 *   1. main() 함수 시작 시 Init() 호출
 *   2. json 파서를 이용해 파일 내용을 구조체(ServerConfig, DatabaseConfig)로 매핑
 *   3. 이후 시스템(DBConnectionPool 등)에서 전역 객체 GConfigManager를 참조하여 초기화
 */
class ConfigManager
{
public:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // 싱글톤(단일 인스턴스) 속성을 유지하기 위해 복사 및 대입을 금지합니다.
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // Config.json 파일을 읽고 파싱하는 함수
    bool Init(const std::string& path);

public:
    ServerConfig serverConfig;     // 파싱된 서버 설정
    DatabaseConfig databaseConfig; // 파싱된 DB 설정
    LogSettings logSettings;       // [TD-01] 파싱된 로그 설정 (섹션이 없으면 기본값)
};
