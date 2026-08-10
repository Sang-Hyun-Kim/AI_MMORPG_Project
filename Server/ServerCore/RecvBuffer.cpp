#include "CorePch.h"
#include "RecvBuffer.h"

/*--------------
	RecvBuffer
----------------*/

RecvBuffer::RecvBuffer(int32 bufferSize) : _bufferSize(bufferSize)
{
	_capacity = bufferSize * BUFFER_COUNT;
	_buffer.resize(_capacity);
}

RecvBuffer::~RecvBuffer()
{
}

void RecvBuffer::Clean()
{
	int32 dataSize = DataSize();
	if (dataSize == 0)
	{
		_readPos = _writePos = 0;
	}
	else
	{
		if (FreeSize() < _bufferSize)
		{
			std::ranges::copy(_buffer.begin() + _readPos, _buffer.begin() + _writePos, _buffer.begin());
			_readPos = 0;
			_writePos = dataSize;
		}
	}
}

bool RecvBuffer::OnRead(int32 numOfBytes)
{
	if (numOfBytes > DataSize())
		return false;

	_readPos += numOfBytes;
	return true;
}

bool RecvBuffer::OnWrite(int32 numOfBytes)
{
	if (numOfBytes > FreeSize())
		return false;

	_writePos += numOfBytes;
	return true;
}

std::span<std::byte> RecvBuffer::ReadSpan()
{
	return std::span<std::byte>(&_buffer[_readPos], DataSize());
}

std::span<std::byte> RecvBuffer::WriteSpan()
{
	return std::span<std::byte>(&_buffer[_writePos], FreeSize());
}
