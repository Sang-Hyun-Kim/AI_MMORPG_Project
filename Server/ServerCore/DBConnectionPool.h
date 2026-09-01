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
 * DBConnection
 * 역할: 단일 MySQL 연결(libmysqlclient의 MYSQL 객체)을 래핑하는 클래스입니다.
 * 특징: 
 *   - RAII 패턴으로 관리하기 위해 객체 소멸 시 자동으로 연결을 해제합니다.
 *   - Execute() 메서드를 통해 SQL 쿼리를 동기적으로 실행합니다.
 */
class DBConnection
{
public:
    DBConnection();
    ~DBConnection();

    bool Connect(const std::string& host, int port, const std::string& user, const std::string& password, const std::string& dbName);
    void Disconnect();

    // 쿼리 실행 (스냅샷/저장용)
    bool Execute(const std::string& query);
    
    // TODO: Fetch/PreparedStatement 등 추가 가능

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
