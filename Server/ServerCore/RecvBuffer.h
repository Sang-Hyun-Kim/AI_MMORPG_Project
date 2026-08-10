#pragma once

/*--------------
	RecvBuffer
----------------*/

class RecvBuffer
{
	enum { BUFFER_COUNT = 10 };

public:
	RecvBuffer(int32 bufferSize);
	~RecvBuffer();

	void					Clean();
	bool					OnRead(int32 numOfBytes);
	bool					OnWrite(int32 numOfBytes);

	std::span<std::byte>	ReadSpan();
	std::span<std::byte>	WriteSpan();
	int32					DataSize() const { return _writePos - _readPos; }
	int32					FreeSize() const { return _capacity - _writePos; }

private:
	int32					_capacity = 0;
	int32					_bufferSize = 0;
	int32					_readPos = 0;
	int32					_writePos = 0;
	std::vector<std::byte>	_buffer;
};
