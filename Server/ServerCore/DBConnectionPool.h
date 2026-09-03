#pragma once
#include "CorePch.h"
#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>

/*
 * DBResult
 * 역할: SELECT 결과 셋(MYSQL_RES)의 수명을 RAII로 관리하는 래퍼입니다.
 *
 * [왜 이 클래스가 필요한가 — 결함 ID F8]
 *   libmysqlclient는 SELECT 실행 후 반드시 mysql_store_result()로 결과를 가져오고
 *   mysql_free_result()로 해제해야 합니다. 이 해제를 빠뜨리면 결과 셋이 커넥션에
 *   남은 채로 남고, 그 커넥션이 풀에 반납되면 **다음에 그 커넥션을 빌린 전혀 다른
 *   세션의 쿼리가** 아래 오류로 실패합니다.
 *
 *       Error 2014 (CR_COMMANDS_OUT_OF_SYNC)
 *       "Commands out of sync; you can't run this command now"
 *
 *   커넥션 풀 구조라 피해가 호출자 본인이 아니라 무관한 세션으로 번지므로
 *   재현과 원인 추적이 매우 어렵습니다. 그래서 해제를 "사람이 기억해야 하는 절차"가
 *   아니라 **소멸자가 보장하는 불변식**으로 만들었습니다.
 *
 * [주의]
 *   - 복사 금지(이중 해제 방지), 이동만 허용합니다.
 *   - 반환된 MYSQL_ROW의 각 칸은 NULL일 수 있습니다. 반드시 널 검사 후 사용하세요.
 *   - MYSQL_ROW가 가리키는 메모리는 이 DBResult가 살아 있는 동안에만 유효합니다.
 */
class DBResult
{
public:
    explicit DBResult(MYSQL_RES* res) : _res(res) {}
    ~DBResult() { Reset(); }

    // 복사 금지: 두 객체가 같은 MYSQL_RES를 해제하면 이중 해제로 크래시합니다.
    DBResult(const DBResult&) = delete;
    DBResult& operator=(const DBResult&) = delete;

    DBResult(DBResult&& other) noexcept : _res(other._res) { other._res = nullptr; }
    DBResult& operator=(DBResult&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            _res = other._res;
            other._res = nullptr;
        }
        return *this;
    }

    // 결과 셋이 유효하고 최소 1행 이상인지 여부
    bool HasRow() const { return _res != nullptr && mysql_num_rows(_res) > 0; }

    // 다음 행을 반환합니다. 더 이상 행이 없으면 nullptr.
    MYSQL_ROW FetchRow() { return _res != nullptr ? mysql_fetch_row(_res) : nullptr; }

    // 현재 행의 칸 개수
    uint32 GetFieldCount() const { return _res != nullptr ? mysql_num_fields(_res) : 0; }

private:
    void Reset()
    {
        if (_res != nullptr)
        {
            mysql_free_result(_res);
            _res = nullptr;
        }
    }

private:
    MYSQL_RES* _res = nullptr;
};

/*
 * DBConnection
 * 역할: 단일 MySQL 연결(libmysqlclient의 MYSQL 객체)을 래핑하는 클래스입니다.
 * 특징:
 *   - RAII 패턴으로 관리하기 위해 객체 소멸 시 자동으로 연결을 해제합니다.
 *   - Execute()      : INSERT/UPDATE/DELETE 등 결과 셋이 없는 쿼리
 *   - ExecuteQuery() : SELECT 등 결과 셋이 있는 쿼리
 */
class DBConnection
{
public:
    DBConnection();
    ~DBConnection();

    bool Connect(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbName);
    void Disconnect();

    /*
     * Execute — 결과 셋을 반환하지 않는 쿼리 전용 (INSERT / UPDATE / DELETE / DDL)
     *
     * ⚠️⚠️ 이 함수에 SELECT를 넘기지 마십시오. ⚠️⚠️
     *   Execute()는 mysql_query()만 호출하고 mysql_store_result()를 부르지 않습니다.
     *   SELECT를 넘기면 읽히지 않은 결과 셋이 커넥션에 남고, 그 커넥션이 풀에 반납되어
     *   **다음에 그 커넥션을 빌린 무관한 세션의 쿼리가 오류 2014로 실패합니다.** [F8]
     *   SELECT는 반드시 아래 ExecuteQuery()를 쓰십시오.
     *
     *   (2026-09-04 이전, GameSession::LoadPlayerTask에 이 함수로 SELECT를 실행하는
     *    코드가 주석 상태로 남아 있었습니다. 다음 작업자가 "친절한 힌트"로 오인해
     *    주석을 해제할 위험이 있어 경고 문구로 대체했습니다.)
     */
    bool Execute(const std::string& query);

    /*
     * ExecuteQuery — 결과 셋을 반환하는 쿼리 전용 (SELECT)
     * 반환: DBResult (실패 시 HasRow()가 false인 빈 결과)
     * 결과 셋 해제는 DBResult 소멸자가 보장하므로 호출자가 신경 쓸 필요가 없습니다.
     */
    DBResult ExecuteQuery(const std::string& query);

    // 직전 INSERT/UPDATE/DELETE가 실제로 영향을 준 행의 수.
    // UPDATE가 0행을 갱신해도 mysql_query는 성공을 반환하므로,
    // "저장했다"고 로그를 찍기 전에 이 값을 확인해야 합니다. [F7]
    uint64 GetAffectedRows() const;

    // 직전 INSERT로 생성된 AUTO_INCREMENT 값
    uint64 GetLastInsertId() const;

    // 문자열 리터럴을 SQL에 안전하게 넣기 위한 이스케이프.
    // (정도(正道)는 Prepared Statement이며 데모 후 과제로 기록되어 있습니다.)
    std::string EscapeString(const std::string& raw) const;

    MYSQL* GetRawConnection() { return _conn; }

private:
    MYSQL* _conn = nullptr;
};

/*
 * DBConnectionPool
 * 역할: 여러 개의 DBConnection 객체를 미리 생성해두고(Pool), 
 *       멀티스레드 환경에서 안전하게 대여(Pop)하고 반납(Push)받는 클래스입니다.
 * 구조:
 *   1) Connection Pool: 미리 연결된 DBConnection의 큐(_connections)
 *   2) Worker Threads: 비동기 DB 작업(람다)을 실행하는 백그라운드 스레드들(_workerThreads)
 * 흐름:
 *   - 코루틴(DBAwaitable)에서 PushJob()을 호출하여 DB 작업을 큐(_jobs)에 넣습니다.
 *   - 여러 DB 워커 스레드 중 하나가 깨어나 큐에서 작업을 꺼냅니다(Pop).
 *   - 워커 스레드는 Connection Pool에서 DBConnection을 하나 빌려옵니다.
 *   - 작업을 실행한 뒤, Connection을 다시 Pool에 반납(Push)합니다.
 */
class DBConnectionPool
{
public:
    DBConnectionPool();
    ~DBConnectionPool();

    // 싱글톤 속성 유지를 위해 복사/대입 금지
    DBConnectionPool(const DBConnectionPool&) = delete;
    DBConnectionPool& operator=(const DBConnectionPool&) = delete;

    bool Connect(int32 connectionCount, const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbName);
    void Clear();

    // 동기식 직접 획득: 사용 가능한 커넥션이 없으면 블로킹(대기)됩니다.
    // 주의: 게임 로직(메인 스레드)에서 직접 호출하면 프레임 드랍이 발생할 수 있습니다.
    DBConnection* Pop();
    // 사용이 끝난 커넥션을 풀에 반환합니다.
    void Push(DBConnection* connection);

    // 비동기 작업 큐 (코루틴 및 백그라운드 작업용)
    // 인자로 전달된 job(람다 함수)은 워커 스레드에서 DBConnection을 할당받아 실행됩니다.
    void PushJob(std::function<void(DBConnection*)> job);

private:
    void WorkerThread();

private:
    std::mutex _lock;
    std::condition_variable _cv;
    std::queue<DBConnection*> _connections;

    std::mutex _jobLock;
    std::condition_variable _jobCv;
    std::queue<std::function<void(DBConnection*)>> _jobs;
    std::vector<std::thread> _workerThreads;
    std::atomic<bool> _stop = false;
};
