#include "CorePch.h"
#include "DBConnectionPool.h"
#include <iostream>

namespace
{
    // DB 워커 스레드의 mysql_thread_init / mysql_thread_end 짝을 RAII 로 맞춥니다. [TD-04 K3 · DB7]
    // ⚠️ 지우면 재연결(mysql_init)한 워커마다 스레드별 블록이 해제되지 않습니다(Debug libmysql 실측 96 bytes/스레드).
    struct MySqlThreadScope
    {
        MySqlThreadScope() { ::mysql_thread_init(); }
        ~MySqlThreadScope() { ::mysql_thread_end(); }
        MySqlThreadScope(const MySqlThreadScope&) = delete;
        MySqlThreadScope& operator=(const MySqlThreadScope&) = delete;
    };
}

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

bool DBConnection::Connect(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbName,
                           const DbReliabilityOptions& options)
{
    if (_conn == nullptr)
    {
        _conn = mysql_init(nullptr);
    }

    // [TD-04 K1] 재연결(K2)에 필요하므로 접속 정보를 보관합니다. ⚠️ _password 는 로그 금지.
    _host = host;
    _port = port;
    _user = user;
    _password = password;
    _dbName = dbName;
    _options = options;

    ApplyOptions(_options);

    MYSQL* ret = mysql_real_connect(_conn, host.c_str(), user.c_str(), password.c_str(), dbName.c_str(), port, nullptr, 0);
    if (ret == nullptr)
    {
        MLOG_ERROR(Db) << "MySQL Connection Error: " << mysql_error(_conn);
        return false;
    }

    // 인코딩 설정
    mysql_set_character_set(_conn, "utf8mb4");
    TouchLastUsedTick(); // [TD-04 K2] 갓 연결된 커넥션은 "방금 쓴" 상태 — 첫 대여에서 불필요한 ping 을 피합니다
    return true;
}

bool DBConnection::IsAlive()
{
    if (_conn == nullptr)
        return false;

    return mysql_ping(_conn) == 0;
}

bool DBConnection::Reconnect()
{
    // 보관값은 Connect 가 같은 멤버에 다시 대입하므로, 자기 참조를 피하려 지역 복사본을 넘깁니다. [TD-04 K2]
    const std::string host = _host;
    const int port = _port;
    const std::string user = _user;
    const std::string password = _password; // ⚠️ 로그로 내보내지 마십시오
    const std::string dbName = _dbName;
    const DbReliabilityOptions options = _options;

    Disconnect();
    return Connect(host, port, user, password, dbName, options);
}

void DBConnection::TouchLastUsedTick()
{
    _lastUsedTick = ::GetTickCount64();
}

/*
 * ApplyOptions
 * 역할: 타임아웃 3종을 커넥션에 적용합니다. [TD-04 K1]
 * ⚠️ 반드시 mysql_real_connect **전에** 호출해야 합니다. 특히 접속 타임아웃이 없으면
 *    호스트가 응답하지 않을 때 OS TCP 기본값(수십 초) × 커넥션 수만큼 기동이 매달려
 *    "기동 Fail-Fast" 라는 이름이 무의미해집니다.
 */
void DBConnection::ApplyOptions(const DbReliabilityOptions& options)
{
    if (_conn == nullptr)
        return;

    // 0 이하는 "무제한"이 되어 위 위험이 되살아나므로 최소 1초로 보정합니다.
    const unsigned int connectSec = static_cast<unsigned int>(options.connectTimeoutSec > 0 ? options.connectTimeoutSec : 1);
    const unsigned int readSec = static_cast<unsigned int>(options.readTimeoutSec > 0 ? options.readTimeoutSec : 1);
    const unsigned int writeSec = static_cast<unsigned int>(options.writeTimeoutSec > 0 ? options.writeTimeoutSec : 1);

    if (mysql_options(_conn, MYSQL_OPT_CONNECT_TIMEOUT, &connectSec) != 0 ||
        mysql_options(_conn, MYSQL_OPT_READ_TIMEOUT, &readSec) != 0 ||
        mysql_options(_conn, MYSQL_OPT_WRITE_TIMEOUT, &writeSec) != 0)
    {
        MLOG_WARN(Db) << "MySQL timeout options not fully applied: " << mysql_error(_conn);
    }
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
        MLOG_ERROR(Db) << "MySQL Query Error: " << mysql_error(_conn) << " (Query: " << query << ")";
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
        MLOG_ERROR(Db) << "MySQL Query Error: " << mysql_error(_conn) << " (Query: " << query << ")";
        return DBResult(nullptr);
    }

    // store_result는 결과 셋을 클라이언트 메모리로 모두 가져옵니다.
    // 이 호출을 빠뜨리면 결과가 커넥션에 남아 다음 쿼리가 오류 2014로 실패합니다.
    MYSQL_RES* res = mysql_store_result(_conn);
    if (res == nullptr)
    {
        // 결과 셋이 없는 정상 케이스(UPDATE 등)와 실제 오류를 구분합니다.
        if (mysql_field_count(_conn) != 0)
            MLOG_ERROR(Db) << "MySQL StoreResult Error: " << mysql_error(_conn);
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

bool DBConnectionPool::Connect(int32 connectionCount, const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbName,
                               const DbReliabilityOptions& options)
{
    _options = options;

    // [TD-04 K1] 실패 시 "지금까지 만든 것"을 전부 되돌리기 위해 지역 컨테이너에 모았다가 한 번에 옮깁니다.
    //   중간에 예외가 끼어들어도 unique_ptr 소멸자가 정리하므로 "false 를 반환하면 자원 0" 계약이
    //   코드 구조 자체로 보장됩니다. 여기를 raw 포인터로 되돌리지 마십시오.
    std::vector<std::unique_ptr<DBConnection>> made;
    made.reserve(static_cast<size_t>(connectionCount));

    for (int32 i = 0; i < connectionCount; i++)
    {
        auto connection = std::make_unique<DBConnection>();
        connection->SetId(i); // [TD-04 K2] 상태 전이 로그에서 어느 커넥션인지 구분하기 위한 번호
        if (connection->Connect(host, port, user, password, dbName, _options) == false)
        {
            // 실패 사유(mysql_error)는 DBConnection::Connect 가 이미 ERROR 로 남겼습니다.
            // 여기서는 "몇 개째에서 멈췄는가"만 더합니다 — ⚠️ password 는 절대 쓰지 않습니다.
            MLOG_ERROR(Db) << "[DBConnectionPool] startup connect failed at " << (i + 1) << "/" << connectionCount
                           << " host=" << host << ":" << port << " db=" << dbName;
            return false; // made 의 소멸자가 앞서 만든 커넥션 전부를 닫습니다
        }

        made.push_back(std::move(connection));
    }

    {
        std::lock_guard<std::mutex> lock(_lock);
        for (std::unique_ptr<DBConnection>& c : made)
            _connections.push(c.release()); // 소유권을 풀로 이전(기존 raw 포인터 관리 방식 유지)
    }

    // Worker Threads 시작 — 커넥션이 전부 준비된 뒤에만 도달합니다.
    for (int32 i = 0; i < connectionCount; i++)
    {
        // [TD-01] 워커에 이름(DB-1..N)을 붙여 어느 워커가 코루틴을 재개했는지 로그로 구분합니다.
        // ⚠️ [TD-04 K0] SetThreadName 은 terminate 핸들러 설치까지 겸합니다. 제거하지 마십시오(V42 재발).
        _workerThreads.push_back(std::thread([this, i]() {
            Logger::SetThreadName("DB", i + 1);
            MySqlThreadScope mysqlThread; // [TD-04 K3] WorkerThread 의 모든 return 경로에서 mysql_thread_end 보장
            WorkerThread();
        }));
    }

    // [TD-04 K1] 적용된 실효 정책값을 기동 로그에 남깁니다 — 설정을 바꿨는지/먹었는지를
    //   Debug·Release 어느 쪽에서도 로그만으로 확인할 수 있어야 합니다.
    MLOG_INFO(Db) << "[DBConnectionPool] connected pool=" << connectionCount
                  << " host=" << host << ":" << port << " db=" << dbName
                  << " connectTimeout=" << _options.connectTimeoutSec << "s read=" << _options.readTimeoutSec
                  << "s write=" << _options.writeTimeoutSec << "s pingIdle=" << _options.pingIdleMs
                  << "ms reconnect=" << _options.reconnectAttempts << "x" << _options.reconnectBackoffMs << "ms";
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

/*
 * EnsureHealthy
 * 역할: 대여 직후 커넥션의 생존을 확인하고, 죽었으면 되살립니다. [TD-04 K2 · DB1]
 * 데이터 흐름: ① 최근에 쓴 커넥션은 검사 생략(정상 부하에서 추가 왕복 0) → ② 오래 쉰 것만 ping →
 *              ③ 죽었으면 상한 횟수만큼 재연결 → ④ 끝내 실패해도 **폐기하지 않는다**.
 * ⚠️ 유지 지침: ④ 에서 커넥션을 버리면 풀 크기가 줄어(불변식 I1 위반) 결국 Pop() 에서 전 워커가
 *    영구 대기합니다. 그리고 이 함수의 반환값으로 작업 실행을 건너뛰지 마십시오(불변식 I2).
 */
bool DBConnectionPool::EnsureHealthy(DBConnection* connection)
{
    if (connection == nullptr)
        return false;

    const uint64 idleMs = ::GetTickCount64() - connection->GetLastUsedTick();
    if (_options.pingIdleMs != 0 && idleMs < _options.pingIdleMs)
        return true; // 방금 쓴 커넥션 — 그 사이에 끊겼다면 쿼리 실패로 드러납니다

    if (connection->IsAlive())
    {
        connection->TouchLastUsedTick();
        return true;
    }

    // 여기부터가 "가동 중 끊김" 경로 — 상태 전이를 반드시 로그로 남깁니다(완료 기준 2 · 불변식 I3)
    MLOG_WARN(Db) << "[DBConnectionPool] connection dead (ping failed) conn=" << connection->GetId()
                  << " idleMs=" << idleMs << "; reconnecting";

    for (int32 attempt = 1; attempt <= _options.reconnectAttempts; ++attempt)
    {
        if (connection->Reconnect())
        {
            MLOG_INFO(Db) << "[DBConnectionPool] reconnected conn=" << connection->GetId()
                          << " attempts=" << attempt;
            return true;
        }

        if (attempt < _options.reconnectAttempts)
            std::this_thread::sleep_for(std::chrono::milliseconds(_options.reconnectBackoffMs));
    }

    MLOG_ERROR(Db) << "[DBConnectionPool] reconnect failed conn=" << connection->GetId()
                   << "; kept in pool (size unchanged), job will run on a dead connection";
    return false;
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

        // [TD-02 B3] 작업이 던져도 lease 소멸자가 반납 → 워커 계속. (DBAwaitable 작업은 B4b 가 먼저 잡음)
        ConnectionLease lease(*this, Pop());
        if (lease.conn)
        {
            // [TD-04 K2] 결과와 무관하게 아래 job 은 반드시 실행한다 — 불변식 I2(결정 D11 (a)).
            EnsureHealthy(lease.conn);
            try
            {
                job(lease.conn);
                lease.conn->TouchLastUsedTick();
            }
            catch (const std::exception& e)
            {
                MLOG_ERROR(Db) << "[DBConnectionPool] job threw: " << e.what();
            }
            catch (...)
            {
                MLOG_ERROR(Db) << "[DBConnectionPool] job threw: unknown exception";
            }
        }
    }
}
