#include "CorePch.h"
#include "ThreadManager.h"

// CoreTLS.h는 CorePch.h에 포함되어 있으므로 바로 사용 가능

ThreadManager::ThreadManager()
{
	InitTLS();
}

ThreadManager::~ThreadManager()
{
	Join();
}

void ThreadManager::Launch(std::function<void(std::stop_token)> callback)
{
	std::lock_guard<std::mutex> guard(_lock);

	// std::jthread는 생성과 동시에 스레드를 구동합니다.
	// 람다를 래핑하여 구동 전후로 TLS 설정을 해줍니다.
	_threads.emplace_back([=](std::stop_token stoken)
	{
		InitTLS();
		
		// 실제 유저가 넘긴 로직 실행 (stop_token을 넘겨서 루프 탈출 신호를 감지할 수 있게 함)
		callback(stoken);
		
		DestroyTLS();
	});
}

void ThreadManager::Join()
{
	std::lock_guard<std::mutex> guard(_lock);
	
	// C++20 std::jthread는 request_stop()을 통해 중단 신호를 보낼 수 있음
	for (std::jthread& t : _threads)
	{
		if (t.joinable())
		{
			t.request_stop(); // 중단 요청 (stop_token 상태 변경)
			// jthread는 소멸자에서 알아서 join을 하지만, 명시적으로 join 대기
			t.join();
		}
	}
	_threads.clear();
}

void ThreadManager::InitTLS()
{
	static std::atomic<uint32> SThreadId = 1;
	LThreadId = SThreadId.fetch_add(1);
}

void ThreadManager::DestroyTLS()
{
	// 스레드 종료 시 명시적으로 정리할 데이터 처리
}
