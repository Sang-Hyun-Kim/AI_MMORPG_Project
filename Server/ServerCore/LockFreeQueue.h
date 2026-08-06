#pragma once

#include "CorePch.h"

/*-----------------------------------------------------------
	MPSC (Multi-Producer Single-Consumer) Lock-Free Queue
	- 다수의 IOCP 워커 스레드가 일감을 넣고(Push), 
	  단 하나의 스레드가 일감을 빼서(Pop) 처리하는 JobQueue에 최적화
	- C++20 std::atomic과 memory_order를 활용하여 Lock(Mutex) 배제
------------------------------------------------------------*/

template<typename T>
class LockFreeQueue
{
private:
	struct Node
	{
		T data;
		std::atomic<Node*> next;
		
		Node() : next(nullptr) {}
		Node(const T& data) : data(data), next(nullptr) {}
		Node(T&& data) : data(std::move(data)), next(nullptr) {}
	};

	std::atomic<Node*> _head;
	std::atomic<Node*> _tail;

public:
	LockFreeQueue()
	{
		// Dummy Node 삽입 (비어있는 상태 처리)
		Node* dummy = new Node();
		_head.store(dummy, std::memory_order_relaxed);
		_tail.store(dummy, std::memory_order_relaxed);
	}

	~LockFreeQueue()
	{
		T data;
		while (Pop(data)) {}
		
		Node* tail = _tail.load(std::memory_order_relaxed);
		if (tail)
			delete tail;
	}

	// 다중 스레드에서 자유롭게 호출 가능 (Wait-Free)
	void Push(const T& data)
	{
		Node* node = new Node(data);
		
		// 1. _head를 새 노드로 교체 (atomic 원자적 연산)
		Node* prev = _head.exchange(node, std::memory_order_acq_rel);
		
		// 2. 이전 헤드의 next를 새 노드로 연결
		prev->next.store(node, std::memory_order_release);
	}

	void Push(T&& data)
	{
		Node* node = new Node(std::move(data));
		Node* prev = _head.exchange(node, std::memory_order_acq_rel);
		prev->next.store(node, std::memory_order_release);
	}

	// 단일 스레드(Consumer)에서만 호출해야 안전 (Lock-Free)
	bool Pop(T& data)
	{
		Node* tail = _tail.load(std::memory_order_relaxed);
		Node* next = tail->next.load(std::memory_order_acquire);
		
		if (next == nullptr)
		{
			// 큐가 비어있거나, Push 스레드가 exchange는 성공했지만 아직 store를 못한 찰나의 상태
			return false;
		}

		data = std::move(next->data);
		_tail.store(next, std::memory_order_release);
		
		// 더미 노드였던 이전 tail을 삭제
		delete tail;
		return true;
	}
};
