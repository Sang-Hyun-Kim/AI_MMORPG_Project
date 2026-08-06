#include "CorePch.h"
#include "SendBuffer.h"

SendBuffer::SendBuffer(int32 allocSize) : _allocSize(allocSize)
{
	// 할당된 크기만큼 vector의 크기를 키워둡니다.
	_buffer.resize(allocSize);
}

SendBuffer::~SendBuffer()
{
}

std::span<std::byte> SendBuffer::Buffer()
{
	// C++20: 연속된 메모리의 첫 주소와 크기를 감싸서 span 객체 반환
	return std::span<std::byte>(_buffer.data(), _allocSize);
}

std::span<std::byte> SendBuffer::Close(int32 writeSize)
{
	_writeSize = writeSize;
	// 실제 쓰여진 만큼의 뷰(View)만 리턴
	return std::span<std::byte>(_buffer.data(), _writeSize);
}
