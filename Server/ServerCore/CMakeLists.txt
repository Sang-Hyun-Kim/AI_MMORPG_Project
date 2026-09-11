# ServerCore 라이브러리 설정
add_library(ServerCore STATIC
    CorePch.cpp
    ThreadManager.cpp
    JobQueue.cpp
    SendBuffer.cpp
    RecvBuffer.cpp
    Session.cpp
    IocpCore.cpp
    IocpEvent.cpp
    Listener.cpp
    NetAddress.cpp
    Service.cpp
    SocketUtils.cpp
    BufferReader.cpp
    BufferWriter.cpp
    CoreGlobal.cpp
    CoreTLS.cpp
    GlobalQueue.cpp
    JobTimer.cpp
    RedisManager.cpp
    Enum.pb.cc
    Struct.pb.cc
    Protocol.pb.cc
    ConfigManager.cpp
    DBConnectionPool.cpp
    Logger.cpp            # [TD-01] 구조적 로그 시스템
)

# 인클루드 경로 설정 (현재 디렉토리)
target_include_directories(ServerCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Protobuf 및 Abseil 링크 (필요한 타겟을 추가)
target_link_libraries(ServerCore PUBLIC
    protobuf::libprotobuf
    redis++::redis++
    unofficial::libmysql::libmysql
)

# 미리 컴파일된 헤더 (Precompiled Header) 설정
# C++20 표준에 맞는 PCH 설정
target_precompile_headers(ServerCore PUBLIC CorePch.h)
