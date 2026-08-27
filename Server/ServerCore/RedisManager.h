#pragma once
#include "CorePch.h"
#include <sw/redis++/redis++.h>

class RedisManager
{
public:
	RedisManager();
	~RedisManager();

	bool Connect(const std::string& uri = "tcp://127.0.0.1:6379");
	void Disconnect();

	// 전역 Redis 객체 획득
	std::shared_ptr<sw::redis::Redis> GetRedis() { return _redis; }

private:
	std::shared_ptr<sw::redis::Redis> _redis;
};

extern std::shared_ptr<RedisManager> GRedisManager;
