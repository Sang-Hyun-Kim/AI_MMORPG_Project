#pragma once
#include "CorePch.h"

/*----------------
	BufferWriter
-----------------*/

class BufferWriter
{
public:
	BufferWriter() = default;
	BufferWriter(std::span<std::byte> buffer, uint32 pos = 0);
	~BufferWriter() = default;

	std::span<std::byte> Buffer() { return _buffer; }
	uint32			Size() { return static_cast<uint32>(_buffer.size()); }
	uint32			WriteSize() { return _pos; }
	uint32			FreeSize() { return static_cast<uint32>(_buffer.size()) - _pos; }

	template<typename T>
	bool			Write(T* src) { return Write(src, sizeof(T)); }
	bool			Write(void* src, uint32 len);

	template<typename T>
	T*				Reserve();

	template<typename T>
	BufferWriter&	operator<<(T&& src);

private:
	std::span<std::byte> _buffer;
	uint32			_pos = 0;
};

template<typename T>
T* BufferWriter::Reserve()
{
	if (FreeSize() < sizeof(T))
		return nullptr;

	T* ret = reinterpret_cast<T*>(_buffer.data() + _pos);
	_pos += sizeof(T);
	return ret;
}

template<typename T>
BufferWriter& BufferWriter::operator<<(T&& src)
{
	using DataType = std::remove_reference_t<T>;
	if (FreeSize() < sizeof(DataType))
		ASSERT_CRASH(false);
		
	*reinterpret_cast<DataType*>(_buffer.data() + _pos) = std::forward<DataType>(src);
	_pos += sizeof(DataType);
	return *this;
}
