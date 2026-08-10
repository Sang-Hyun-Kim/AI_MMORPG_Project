#pragma once
#include "CorePch.h"

/*----------------
	BufferReader
-----------------*/

class BufferReader
{
public:
	BufferReader() = default;
	BufferReader(std::span<std::byte> buffer, uint32 pos = 0);
	~BufferReader() = default;

	std::span<std::byte> Buffer() { return _buffer; }
	uint32			Size() { return static_cast<uint32>(_buffer.size()); }
	uint32			ReadSize() { return _pos; }
	uint32			FreeSize() { return static_cast<uint32>(_buffer.size()) - _pos; }

	template<typename T>
	bool			Peek(T* dest) { return Peek(dest, sizeof(T)); }
	bool			Peek(void* dest, uint32 len);

	template<typename T>
	bool			Read(T* dest) { return Read(dest, sizeof(T)); }
	bool			Read(void* dest, uint32 len);

	template<typename T>
	BufferReader&	operator>>(OUT T& dest);

private:
	std::span<std::byte> _buffer;
	uint32			_pos = 0;
};

template<typename T>
inline BufferReader& BufferReader::operator>>(OUT T& dest)
{
	if (FreeSize() < sizeof(T))
		ASSERT_CRASH(false);

	dest = *reinterpret_cast<T*>(_buffer.data() + _pos);
	_pos += sizeof(T);
	return *this;
}
