#pragma once

extern class ThreadManager* GThreadManager;
extern class GlobalQueue* GGlobalQueue;
extern class JobTimer* GJobTimer;

extern std::shared_ptr<class ConfigManager> GConfigManager;
extern std::shared_ptr<class DBConnectionPool> GDBConnectionPool;
