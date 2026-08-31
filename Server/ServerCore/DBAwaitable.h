#pragma once
#include "CorePch.h"
#include "DBConnectionPool.h"
#include <coroutine>

/*
 * DBAwaitable
 * 역할: C++20 코루틴(co_await) 메커니즘을 지원하기 위한 Awaiter 인터페이스 구현체입니다.
 * 원리:
 *   1) 게임 로직에서 `co_await DBAwaitable(...)`을 호출하면, 현재 스레드(GameRoom)의 실행이 '일시 중단(Suspend)' 됩니다.
 *   2) await_suspend() 내부에서 DB 작업을 DB 워커 스레드로 넘깁니다(PushJob).
 *   3) DB 처리가 완료되면, 인자로 넘겨진 원래 큐(resumeQueue)에 코루틴의 남은 부분(resume)을 예약합니다.
 *   4) 다시 게임 스레드가 틱을 돌 때 코루틴이 이어서 실행(Resume)됩니다.
 * 주의사항: 단일 스레드 JobQueue 환경에서 호출되어야 스레드 안전성이 보장됩니다.
 */
class DBAwaitable
{
public:
    DBAwaitable(std::function<void(DBConnection*)> dbJob, std::shared_ptr<class JobQueue> resumeQueue)
        : _dbJob(std::move(dbJob)), _resumeQueue(resumeQueue)
    {
    }

    // co_await 호출 시 즉시 재개할지 여부를 반환합니다.
    // false를 반환하면 항상 일시 중단(Suspend)되고 await_suspend()가 호출됩니다.
    bool await_ready() const noexcept
    {
        return false; 
    }

    // 코루틴이 일시 중단되었을 때 호출되는 콜백입니다.
    // 인자로 전달된 handle은 "이후의 코루틴 상태"를 조작할 수 있는 핸들입니다.
    void await_suspend(std::coroutine_handle<> handle)
    {
        // 1. DB 스레드 풀에 작업을 던집니다.
        // 이때 코루틴 핸들(handle)과 원래 작업 큐(queue)를 람다 캡처로 함께 넘깁니다.
        GDBConnectionPool->PushJob([handle, job = _dbJob, queue = _resumeQueue](DBConnection* conn) {
            
            // 2. DB 스레드에서 실제 DB 작업 실행
            job(conn);

            // 3. 작업이 끝나면 원래 큐(예: GameRoom 큐)로 코루틴 Resume(재개) 작업을 던짐
            if (queue)
            {
                queue->DoAsync([handle]() {
                    handle.resume();
                });
            }
            else
            {
                // Resume Queue가 없으면 DB 스레드에서 바로 Resume (동기화 이슈 주의)
                handle.resume();
            }
        });
    }

    // 코루틴이 재개될 때 호출되며, 결과를 반환할 수 있습니다. (여기서는 void)
    void await_resume() const noexcept
    {
    }

private:
    std::function<void(DBConnection*)> _dbJob;      // DB 워커에서 실행할 실제 쿼리 함수
    std::shared_ptr<class JobQueue> _resumeQueue;   // 재개될 원래 스레드의 큐
};

/*
 * JobTask
 * 역할: C++20 코루틴 함수의 반환 타입으로 사용되는 래퍼 객체입니다.
 * 특징: 
 *   - 함수가 코루틴으로 동작하려면 반드시 컴파일러가 인식할 수 있는 promise_type이 내부에 정의되어야 합니다.
 *   - 이 클래스는 Fire-And-Forget(결과를 반환하거나 기다리지 않음) 방식의 가장 단순한 코루틴 타입을 정의합니다.
 */
struct JobTask {
    struct promise_type {
        JobTask get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; } // 코루틴 진입 시 중단 없이 바로 실행
        std::suspend_never final_suspend() noexcept { return {}; } // 완료 후 그대로 소멸
        void return_void() {}
        void unhandled_exception() {}
    };
};
