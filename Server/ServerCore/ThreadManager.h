#pragma once

#include "CorePch.h"

/*-------------------
	ThreadManager
--------------------*/

class ThreadManager
{
public:
	ThreadManager();
	~ThreadManager();

	// std::jthread를 사용하기 때문에 소멸자에서 알아서 join이 호출됩니다.
	// 람다나 함수 객체를 던져서 새로운 워커 스레드를 구동합니다.
	void	Launch(std::function<void(std::stop_token)> callback);

	// 스레드 풀 강제 중단 및 조인 대기
	void	Join();

	// 전역 TLS(Thread Local Storage) 초기화 및 소멸을 관리하기 위한 함수
	static void InitTLS();
	static void DestroyTLS();

private:
	std::mutex				_lock;
	std::vector<std::jthread> _threads;
};

// 전역(Global) 스레드 매니저
extern ThreadManager GThreadManager;
