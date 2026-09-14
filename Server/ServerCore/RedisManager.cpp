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
			MLOG_INFO(Redis) << "[RedisManager] Connected to Redis: " << LogMask::Uri(uri);
			return true;
		}
	}
	catch (const sw::redis::Error& e)
	{
		MLOG_ERROR(Redis) << "[RedisManager] Connection failed: " << e.what();
	}
	
	_redis = nullptr;
	return false;
}

void RedisManager::Disconnect()
{
	_redis = nullptr;
}
