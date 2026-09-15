# [TD-01 / 2026-09-11] 로거 단독 검증 도구
#   ServerCore 를 링크하지 않고 Logger.cpp 만 함께 컴파일합니다.
#   게임 서버 바이너리와 무관하며, protobuf·MySQL·Redis 없이 로거만 검증합니다.
#   [TD-03] 크래시·terminate 는 프로세스를 끝내므로 자식 프로세스 방식인 이 도구에 남깁니다(ServerTests 와 공존).
#   LoggerSelfTest.cpp 는 로컬 전용(D8) — 파일이 있는 환경에서만 추가합니다.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LoggerSelfTest.cpp")
    add_executable(LoggerSelfTest
        LoggerSelfTest.cpp
        ${CMAKE_SOURCE_DIR}/ServerCore/Logger.cpp
    )

    target_include_directories(LoggerSelfTest PRIVATE ${CMAKE_SOURCE_DIR}/ServerCore)

    # ctest 한 줄 실행: ctest -C Debug --output-on-failure
    add_test(NAME LoggerSelfTest COMMAND LoggerSelfTest --all)
    set_tests_properties(LoggerSelfTest PROPERTIES LABELS "selftest")
endif()

# [TD-03] ServerTests — doctest 단위 시험. GameServer 타깃과 분리(게임 서버 빌드에 끼지 않음)
find_package(doctest CONFIG REQUIRED)

add_executable(ServerTests
    Unit/TestMain.cpp
    Unit/HarnessTests.cpp
    Unit/SabotageTests.cpp
    Unit/SectorMathTests.cpp
)

# SectorMath.h 만 쓰기 위한 경로. GameRoom.h 는 include 하지 않습니다(GameServer 의존이 따라옴).
target_include_directories(ServerTests PRIVATE ${CMAKE_SOURCE_DIR}/GameServer/Game/Room)
target_link_libraries(ServerTests PRIVATE ServerCore doctest::doctest)

# 스위트 단위로 ctest 항목을 나눕니다. 라벨: unit · sabotage (+ selftest)
foreach(suite Harness SectorMath)
    add_test(NAME Unit.${suite} COMMAND ServerTests --test-suite=${suite})
    set_tests_properties(Unit.${suite} PROPERTIES LABELS "unit" TIMEOUT 120)
endforeach()

# 하네스가 실패를 실제로 전달하는지 증명: 환경 변수로 무장한 스위트는 반드시 실패해야 함
add_test(NAME Sabotage.MustFail COMMAND ServerTests --test-suite=Sabotage)
set_tests_properties(Sabotage.MustFail PROPERTIES ENVIRONMENT "MMO_TEST_SABOTAGE=1" WILL_FAIL TRUE LABELS "sabotage")
