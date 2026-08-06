@echo off
REM 1. Protocol.proto 파일 경로
set PROTO_PATH=..\Protobuf\Protocol.proto

REM 2. protoc 실행기 경로 (vcpkg나 다운로드된 protoc 사용)
set PROTOC_PATH=..\Protobuf\protoc.exe

REM 3. C++ Proto 헤더 및 코드 출력 디렉토리
set CPP_OUT=..\..\Server\ServerCore

REM 4. Protoc 컴파일 (C++ 파일 생성)
%PROTOC_PATH% -I=..\Protobuf --cpp_out=%CPP_OUT% %PROTO_PATH%

REM 5. 파이썬 패킷 제너레이터 실행 (Server용 핸들러)
python PacketGenerator.py --path=%PROTO_PATH% --output=ServerPacketHandler --recv=C_ --send=S_
IF ERRORLEVEL 1 PAUSE

REM 6. 파이썬 패킷 제너레이터 실행 (Client용 핸들러)
python PacketGenerator.py --path=%PROTO_PATH% --output=ClientPacketHandler --recv=S_ --send=C_
IF ERRORLEVEL 1 PAUSE

REM 7. 생성된 핸들러 파일을 서버 코어 프로젝트로 이동
move ServerPacketHandler.h %CPP_OUT%\ServerPacketHandler.h
move ServerPacketHandler.cpp %CPP_OUT%\ServerPacketHandler.cpp
move ClientPacketHandler.h %CPP_OUT%\ClientPacketHandler.h
move ClientPacketHandler.cpp %CPP_OUT%\ClientPacketHandler.cpp

echo 패킷 자동화 파이프라인 생성 완료!
pause
