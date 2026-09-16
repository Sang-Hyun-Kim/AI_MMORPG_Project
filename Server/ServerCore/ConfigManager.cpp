#include "CorePch.h"
#include "ConfigManager.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;


bool ConfigManager::Init(const std::string& path)
{
    // JSON 파일 로드 및 예외 처리
    std::ifstream file(path);
    if (!file.is_open())
    {
        MLOG_WARN(Config) << "Failed to open config file: " << path;
        return false;
    }

    try
    {
        json j;
        file >> j;

        // 서버 설정(Server 섹션) 파싱
        // value(key, default_value) 형태를 사용하여 키가 없을 때의 기본값을 보장합니다.
        if (j.contains("Server"))
        {
            serverConfig.bindAddress = j["Server"].value("BindAddress", std::string("0.0.0.0"));
            serverConfig.port = j["Server"].value("Port", 7777);
            serverConfig.maxSession = j["Server"].value("MaxSession", 1000);
        }

        // 데이터베이스 설정(Database 섹션) 파싱
        if (j.contains("Database"))
        {
            if (j["Database"].contains("MySQL"))
            {
                auto& mysqlJson = j["Database"]["MySQL"];
                databaseConfig.mySqlHost = mysqlJson.value("Host", "127.0.0.1");
                databaseConfig.mySqlPort = mysqlJson.value("Port", 3306);
                databaseConfig.mySqlUser = mysqlJson.value("User", "root");
                databaseConfig.mySqlPassword = mysqlJson.value("Password", "");
                databaseConfig.mySqlDatabase = mysqlJson.value("Database", "");

                // [TD-04 K1] 신뢰성 키 6개. 기본값 정본은 DbReliabilityOptions 이므로
                //   여기서는 "키가 없으면 현재 값 유지"로 넘겨 기본값 복제를 피합니다.
                DbReliabilityOptions& rel = databaseConfig.mySqlReliability;
                rel.connectTimeoutSec = mysqlJson.value("ConnectTimeoutSec", rel.connectTimeoutSec);
                rel.readTimeoutSec = mysqlJson.value("ReadTimeoutSec", rel.readTimeoutSec);
                rel.writeTimeoutSec = mysqlJson.value("WriteTimeoutSec", rel.writeTimeoutSec);
                rel.pingIdleMs = mysqlJson.value("PingIdleMs", rel.pingIdleMs);
                rel.reconnectAttempts = mysqlJson.value("ReconnectAttempts", rel.reconnectAttempts);
                rel.reconnectBackoffMs = mysqlJson.value("ReconnectBackoffMs", rel.reconnectBackoffMs);
            }
            databaseConfig.redisString = j["Database"].value("Redis", "");
        }

        // [TD-01] 로그 설정(Log 섹션). 없으면 LogSettings 기본값을 그대로 둡니다.
        if (j.contains("Log"))
        {
            auto& logJson = j["Log"];
            logSettings.async = logJson.value("Async", true);
            logSettings.fileEnabled = logJson.value("File", true);
            logSettings.consoleLevel = Logger::ParseLevel(logJson.value("ConsoleLevel", std::string("Info")), LogLevel::Info);
            logSettings.fileLevel = Logger::ParseLevel(logJson.value("FileLevel", std::string("Info")), LogLevel::Info);
        }

        MLOG_INFO(Config) << "Config loaded successfully. Bind: " << serverConfig.bindAddress
                          << " Port: " << serverConfig.port;
        return true;
    }
    catch (const json::exception& e)
    {
        MLOG_ERROR(Config) << "Config Parse Error: " << e.what();
        return false;
    }
}
