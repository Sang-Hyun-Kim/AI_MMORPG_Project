#include "RedisManager.h"
#include <iostream>

std::shared_ptr<RedisManager> GRedisManager = nullptr;

RedisManager::RedisManager()
{
}

RedisManager::~RedisManager()
{
	Disconnect();
}

bool RedisManager::Connect(const std::string& uri)
{
	try
	{
		_redis = std::make_shared<sw::redis::Redis>(uri);
		// 접속 테스트 (Ping)
		if (_redis->ping() == "PONG")
		{
			std::cout << "[RedisManager] Connected to Redis: " << uri << std::endl;
			return true;
		}
	}
	catch (const sw::redis::Error& e)
	{
		std::cerr << "[RedisManager] Connection failed: " << e.what() << std::endl;
	}
	
	_redis = nullptr;
	return false;
}

void RedisManager::Disconnect()
{
	_redis = nullptr;
}
