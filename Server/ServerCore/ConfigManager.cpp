#include "pch.h"
#include "ConfigManager.h"
#include "json.hpp"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

std::shared_ptr<ConfigManager> GConfigManager = nullptr;

bool ConfigManager::Init(const std::string& path)
{
    // JSON 파일 로드 및 예외 처리
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open config file: " << path << std::endl;
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
            }
            databaseConfig.redisString = j["Database"].value("Redis", "");
        }

        std::cout << "Config loaded successfully. Port: " << serverConfig.port << std::endl;
        return true;
    }
    catch (const json::exception& e)
    {
        std::cerr << "Config Parse Error: " << e.what() << std::endl;
        return false;
    }
}
