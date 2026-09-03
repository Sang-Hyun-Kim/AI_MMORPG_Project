#pragma once
#include <string>
#include <memory>

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

// 데이터베이스(MySQL, Redis) 접속 문자열 및 설정 정보
struct DatabaseConfig
{
    // MySQL 접속 정보 (하드코딩 방지)
    std::string mySqlHost;      
    int32 mySqlPort = 3306;
    std::string mySqlUser;
    std::string mySqlPassword;
    std::string mySqlDatabase;
    
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
};
