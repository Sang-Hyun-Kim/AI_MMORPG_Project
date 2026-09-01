@echo off
set PROTO_PATH=..\Protobuf\Protocol.proto
set PROTOC_PATH=..\..\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe

set CORE_OUT=..\..\Server\ServerCore
set GAME_OUT=..\..\Server\GameServer\Packet
set DUMMY_OUT=..\..\Server\DummyClient\Packet
set UE_OUT=..\..\Client\AI_MMORPG_Client\Source\AI_MMORPG_Client\Network

if not exist %GAME_OUT% mkdir %GAME_OUT%
if not exist %DUMMY_OUT% mkdir %DUMMY_OUT%
if not exist %UE_OUT% mkdir %UE_OUT%

%PROTOC_PATH% -I=..\Protobuf --cpp_out=%CORE_OUT% ..\Protobuf\Enum.proto ..\Protobuf\Struct.proto ..\Protobuf\Protocol.proto
%PROTOC_PATH% -I=..\Protobuf --cpp_out=%UE_OUT% ..\Protobuf\Enum.proto ..\Protobuf\Struct.proto ..\Protobuf\Protocol.proto

python PacketGenerator.py --path=%PROTO_PATH% --output=ServerPacketHandler --recv=C_ --send=S_

python PacketGenerator.py --path=%PROTO_PATH% --output=ClientPacketHandler --recv=S_ --send=C_

move ServerPacketHandler.h %GAME_OUT%\ServerPacketHandler.h

copy ClientPacketHandler.h %DUMMY_OUT%\ClientPacketHandler.h

move ClientPacketHandler.h %UE_OUT%\ClientPacketHandler.h

echo Done!
