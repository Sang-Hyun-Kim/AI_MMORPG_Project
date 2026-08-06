# ServerCore 라이브러리 설정
add_library(ServerCore STATIC
    CorePch.cpp
)

# 인클루드 경로 설정 (현재 디렉토리)
target_include_directories(ServerCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Protobuf 및 Abseil 링크 (필요한 타겟을 추가)
target_link_libraries(ServerCore PUBLIC
    protobuf::libprotobuf
    # absl 타겟들은 필요한 경우 여기에 추가합니다.
)

# 미리 컴파일된 헤더 (Precompiled Header) 설정
# C++20 표준에 맞는 PCH 설정
target_precompile_headers(ServerCore PUBLIC CorePch.h)
