// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// ----------------------------------------------------
// 네트워크 공용 패킷 헤더
// ----------------------------------------------------
#pragma pack(push, 1)
struct PacketHeader
{
	uint16 size;
	uint16 id;
};
#pragma pack(pop)

// ----------------------------------------------------
// 임시 SendBuffer 및 Session (UE 전용)
// ----------------------------------------------------
class SendBuffer
{
public:
	SendBuffer(int32 BufferSize) { BufferArray.SetNumUninitialized(BufferSize); }
	TArray<uint8>& Buffer() { return BufferArray; }
	void Close(uint32 FinalSize) { BufferArray.SetNum(FinalSize); }
private:
	TArray<uint8> BufferArray;
};
using SendBufferRef = TSharedPtr<SendBuffer>;

class PacketSession : public TSharedFromThis<PacketSession>
{
public:
	virtual ~PacketSession() {}
	virtual void Send(SendBufferRef sendBuffer) = 0;
};
using PacketSessionRef = TSharedPtr<PacketSession>;

