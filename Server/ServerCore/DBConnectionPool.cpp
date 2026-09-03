#include "CorePch.h"
#include "DBConnectionPool.h"
#include <iostream>

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

    // ⚠️ SELECT를 넘기지 마십시오. 결과 셋이 소비되지 않아 커넥션이 오염됩니다. [F8]
    //    SELECT는 ExecuteQuery()를 사용하십시오. (상세는 DBConnectionPool.h 주석 참조)
    int ret = mysql_query(_conn, query.c_str());
    if (ret != 0)
    {
        std::cerr << "MySQL Query Error: " << mysql_error(_conn) << " (Query: " << query << ")" << std::endl;
        return false;
    }
    return true;
}

/*
 * ExecuteQuery
 * SELECT 실행 후 결과 셋을 즉시 store하여 DBResult에 담아 반환합니다.
 * DBResult 소멸자가 mysql_free_result를 보장하므로, 호출자가 어느 경로로
 * 빠져나가더라도(조기 return, 예외) 커넥션이 오염되지 않습니다. [F8]
 */
DBResult DBConnection::ExecuteQuery(const std::string& query)
{
    if (_conn == nullptr)
        return DBResult(nullptr);

    if (mysql_query(_conn, query.c_str()) != 0)
    {
        std::cerr << "MySQL Query Error: " << mysql_error(_conn) << " (Query: " << query << ")" << std::endl;
        return DBResult(nullptr);
    }

    // store_result는 결과 셋을 클라이언트 메모리로 모두 가져옵니다.
    // 이 호출을 빠뜨리면 결과가 커넥션에 남아 다음 쿼리가 오류 2014로 실패합니다.
    MYSQL_RES* res = mysql_store_result(_conn);
    if (res == nullptr)
    {
        // 결과 셋이 없는 정상 케이스(UPDATE 등)와 실제 오류를 구분합니다.
        if (mysql_field_count(_conn) != 0)
            std::cerr << "MySQL StoreResult Error: " << mysql_error(_conn) << std::endl;
        return DBResult(nullptr);
    }

    return DBResult(res);
}

uint64 DBConnection::GetAffectedRows() const
{
    if (_conn == nullptr) return 0;

    // mysql_affected_rows는 오류 시 (my_ulonglong)-1을 반환합니다.
    my_ulonglong rows = mysql_affected_rows(_conn);
    if (rows == static_cast<my_ulonglong>(-1))
        return 0;

    return static_cast<uint64>(rows);
}

uint64 DBConnection::GetLastInsertId() const
{
    if (_conn == nullptr) return 0;
    return static_cast<uint64>(mysql_insert_id(_conn));
}

std::string DBConnection::EscapeString(const std::string& raw) const
{
    if (_conn == nullptr) return std::string();

    // 최악의 경우 모든 문자가 이스케이프되어 2배가 되므로 2n+1 확보
    std::string out;
    out.resize(raw.size() * 2 + 1);

    unsigned long len = mysql_real_escape_string(_conn, out.data(), raw.c_str(),
                                                 static_cast<unsigned long>(raw.size()));
    out.resize(len);
    return out;
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
