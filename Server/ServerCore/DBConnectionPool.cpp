#include "pch.h"
#include "DBConnectionPool.h"
#include <iostream>

std::shared_ptr<DBConnectionPool> GDBConnectionPool = nullptr;

/*----------------
    DBConnection
----------------*/

DBConnection::DBConnection()
{
    _conn = mysql_init(nullptr);
}

DBConnection::~DBConnection()
{
    Disconnect();
}

bool DBConnection::Connect(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbName)
{
    if (_conn == nullptr)
    {
        _conn = mysql_init(nullptr);
    }

    MYSQL* ret = mysql_real_connect(_conn, host.c_str(), user.c_str(), password.c_str(), dbName.c_str(), port, nullptr, 0);
    if (ret == nullptr)
    {
        std::cerr << "MySQL Connection Error: " << mysql_error(_conn) << std::endl;
        return false;
    }

    // 인코딩 설정
    mysql_set_character_set(_conn, "utf8mb4");
    return true;
}

void DBConnection::Disconnect()
{
    if (_conn != nullptr)
    {
        mysql_close(_conn);
        _conn = nullptr;
    }
}

bool DBConnection::Execute(const std::string& query)
{
    if (_conn == nullptr) return false;

    int ret = mysql_query(_conn, query.c_str());
    if (ret != 0)
    {
        std::cerr << "MySQL Query Error: " << mysql_error(_conn) << " (Query: " << query << ")" << std::endl;
        return false;
    }
    return true;
}

/*----------------
  DBConnectionPool
----------------*/

DBConnectionPool::DBConnectionPool()
{
}

DBConnectionPool::~DBConnectionPool()
{
    Clear();
}

bool DBConnectionPool::Connect(int32 connectionCount, const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbName)
{
    for (int32 i = 0; i < connectionCount; i++)
    {
        DBConnection* connection = new DBConnection();
        if (connection->Connect(host, port, user, password, dbName) == false)
        {
            delete connection;
            return false;
        }

        _connections.push(connection);
    }

    // Worker Threads 시작
    for (int32 i = 0; i < connectionCount; i++)
    {
        _workerThreads.push_back(std::thread([this]() { WorkerThread(); }));
    }

    return true;
}

void DBConnectionPool::Clear()
{
    _stop = true;
    _jobCv.notify_all();

    for (std::thread& t : _workerThreads)
    {
        if (t.joinable())
            t.join();
    }
    _workerThreads.clear();

    std::lock_guard<std::mutex> lock(_lock);
    while (_connections.empty() == false)
    {
        DBConnection* connection = _connections.front();
        _connections.pop();
        delete connection;
    }
}

DBConnection* DBConnectionPool::Pop()
{
    std::unique_lock<std::mutex> lock(_lock);
    // 조건 변수를 통해 연결이 반환될 때까지 대기
    _cv.wait(lock, [this]() { return _connections.empty() == false; });

    DBConnection* connection = _connections.front();
    _connections.pop();
    return connection;
}

void DBConnectionPool::Push(DBConnection* connection)
{
    std::lock_guard<std::mutex> lock(_lock);
    _connections.push(connection);
    _cv.notify_one();
}

void DBConnectionPool::PushJob(std::function<void(DBConnection*)> job)
{
    std::lock_guard<std::mutex> lock(_jobLock);
    _jobs.push(std::move(job));
    _jobCv.notify_one();
}

void DBConnectionPool::WorkerThread()
{
    // 큐에 남은 작업을 모두 소진(Flush)하기 위해 무한 루프를 돌고 내부에서 탈출 조건을 체크합니다.
    while (true)
    {
        std::function<void(DBConnection*)> job;
        {
            std::unique_lock<std::mutex> lock(_jobLock);
            _jobCv.wait(lock, [this]() { return !_jobs.empty() || _stop; });

            if (_stop && _jobs.empty())
                return;

            job = std::move(_jobs.front());
            _jobs.pop();
        }

        DBConnection* connection = Pop();
        if (connection)
        {
            job(connection);
            Push(connection);
        }
    }
}
