#include "CorePch.h"
#include "BufferWriter.h"

/*----------------
	BufferWriter
-----------------*/

BufferWriter::BufferWriter(std::span<std::byte> buffer, uint32 pos)
	: _buffer(buffer), _pos(pos)
{
}

bool BufferWriter::Write(void* src, uint32 len)
{
	if (FreeSize() < len)
		return false;

	::memcpy(_buffer.data() + _pos, src, len);
	_pos += len;
	return true;
}
