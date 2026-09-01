@echo off
set PROTO_PATH=..\Protobuf\Protocol.proto
set PROTOC_PATH=..\..\vcpkg\installed\x64-windows\tools\protobuf\protoc.exe

set CORE_OUT=..\..\Server\ServerCore
set GAME_OUT=..\..\Server\GameServer\Packet
set DUMMY_OUT=..\..\Server\DummyClient\Packet

if not exist %GAME_OUT% mkdir %GAME_OUT%
if not exist %DUMMY_OUT% mkdir %DUMMY_OUT%

%PROTOC_PATH% -I=..\Protobuf --cpp_out=%CORE_OUT% ..\Protobuf\Enum.proto ..\Protobuf\Struct.proto ..\Protobuf\Protocol.proto

REM UE_OUT is temporarily disabled since the Client project is being reinstalled.
REM python PacketGenerator.py --path=%PROTO_PATH% --output=ClientPacketHandler --recv=S_ --send=C_ --ue_project AMC1
python PacketGenerator.py --path=%PROTO_PATH% --output=ClientPacketHandler --recv=S_ --send=C_

python PacketGenerator.py --path=%PROTO_PATH% --output=ServerPacketHandler --recv=C_ --send=S_

move ServerPacketHandler.h %GAME_OUT%\ServerPacketHandler.h

copy ClientPacketHandler.h %DUMMY_OUT%\ClientPacketHandler.h

echo Done!

echo Done!
