#pragma once

// C++20 모던 헤더
#include <iostream>
#include <vector>
#include <list>
#include <queue>
#include <stack>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <span>       // C++20: 버퍼 경계 검사를 위한 연속된 메모리 뷰
#include <stop_token> // C++20: std::jthread의 안전한 중단을 위한 토큰
#include <concepts>   // C++20: 템플릿 메타 프로그래밍 및 제약
#include <cstddef>    // std::byte

using namespace std;

// Windows API
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
#include <WS2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

// 스마트 포인터 매크로 헬퍼
#define USING_SHARED_PTR(name) \
	using name##Ref = std::shared_ptr<class name>; \
	using name##WeakRef = std::weak_ptr<class name>;

// 크기 타입 재정의
using int8 = __int8;
using int16 = __int16;
using int32 = __int32;
using int64 = __int64;
using uint8 = unsigned __int8;
using uint16 = unsigned __int16;
using uint32 = unsigned __int32;
using uint64 = unsigned __int64;

#define size16(val)		static_cast<int16>(sizeof(val))
#define size32(val)		static_cast<int32>(sizeof(val))
#define len16(arr)		static_cast<int16>(sizeof(arr)/sizeof(arr[0]))
#define len32(arr)		static_cast<int32>(sizeof(arr)/sizeof(arr[0]))

#include <cassert>
#define ASSERT_CRASH(expr)			\
	do {							\
		if (!(expr))				\
		{							\
			__debugbreak();			\
			abort();				\
		}							\
	} while(0)

USING_SHARED_PTR(IocpCore);
USING_SHARED_PTR(IocpObject);
USING_SHARED_PTR(Session);
USING_SHARED_PTR(PacketSession);
USING_SHARED_PTR(Listener);
USING_SHARED_PTR(ServerService);
USING_SHARED_PTR(ClientService);
USING_SHARED_PTR(SendBuffer);
USING_SHARED_PTR(Service);
USING_SHARED_PTR(Job);
USING_SHARED_PTR(JobQueue);

#include "CoreTLS.h"
#include "CoreGlobal.h"
#include "GlobalQueue.h"
#include "JobTimer.h"
