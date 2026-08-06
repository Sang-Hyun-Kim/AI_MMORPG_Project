#pragma once

#include "CorePch.h"

/*----------------
	SendBuffer
-----------------*/

class SendBuffer
{
public:
	// allocSize: 버퍼의 총 용량
	SendBuffer(int32 allocSize);
	~SendBuffer();

	// 전체 버퍼의 뷰를 리턴합니다. (C++20 std::span 활용)
	std::span<std::byte> Buffer();

	// 버퍼 중 실제 쓰인 데이터 영역만큼을 닫고, 그 영역의 뷰를 리턴합니다.
	std::span<std::byte> Close(int32 writeSize);

	int32 WriteSize() const { return _writeSize; }
	int32 Capacity() const { return _allocSize; }

private:
	std::vector<std::byte>	_buffer;
	int32					_allocSize = 0;
	int32					_writeSize = 0;
};

using SendBufferRef = std::shared_ptr<SendBuffer>;
