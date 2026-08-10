cmake_minimum_required(VERSION 3.25)
project(MMORPG_Server VERSION 1.0.0 LANGUAGES CXX)

# 강제로 C++20 표준 적용
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC 컴파일러 최적화 옵션
if(MSVC)
    add_compile_options(/W4 /WX- /EHsc /Zc:preprocessor /MP /utf-8)
    add_compile_definitions(WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
endif()

# 의존성 찾기 (vcpkg 툴체인을 통해 연동됨)
find_package(protobuf CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)

# 하위 디렉토리 추가
add_subdirectory(ServerCore)
add_subdirectory(GameServer)
add_subdirectory(DummyClient)
