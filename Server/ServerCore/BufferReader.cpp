#include "CorePch.h"
#include "BufferReader.h"

/*----------------
	BufferReader
-----------------*/

BufferReader::BufferReader(std::span<std::byte> buffer, uint32 pos)
	: _buffer(buffer), _pos(pos)
{
}

bool BufferReader::Peek(void* dest, uint32 len)
{
	if (FreeSize() < len)
		return false;

	::memcpy(dest, _buffer.data() + _pos, len);
	return true;
}

bool BufferReader::Read(void* dest, uint32 len)
{
	if (Peek(dest, len) == false)
		return false;

	_pos += len;
	return true;
}
