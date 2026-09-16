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

    # [TD-04 K0 · V42 해소] 워커 스레드 terminate 의 예외 메시지가 PANIC 에 기록되는지 — 이제 **정상 기대**.
    #   출력 문구로 판정(종료 코드 무시) — 자식이 크래시로 끝나므로 종료 코드는 0 이 아니다.
    #   이 항목이 실패하면 스레드별 terminate 핸들러 설치가 사라진 것(회귀): Logger::SetThreadName →
    #   InstallThreadCrashHandlers 경로를 먼저 확인할 것. (TD-03 당시 기대값은 "V42 KNOWN-BUG CONFIRMED")
    add_test(NAME V42.WorkerTerminateMessage COMMAND LoggerSelfTest --expect-worker-terminate)
    set_tests_properties(V42.WorkerTerminateMessage PROPERTIES
        PASS_REGULAR_EXPRESSION "V42 FIXED" LABELS "selftest" TIMEOUT 120)
endif()

# [TD-03] ServerTests — doctest 단위 시험. GameServer 타깃과 분리(게임 서버 빌드에 끼지 않음)
find_package(doctest CONFIG REQUIRED)

add_executable(ServerTests
    Unit/TestMain.cpp
    Unit/HarnessTests.cpp
    Unit/SabotageTests.cpp
    Unit/SectorMathTests.cpp
    Unit/LockFreeQueueTests.cpp
    Unit/RecvBufferTests.cpp
    Unit/FramingTests.cpp
    Unit/DbPoolTests.cpp
)

# SectorMath.h 만 쓰기 위한 경로. GameRoom.h 는 include 하지 않습니다(GameServer 의존이 따라옴).
target_include_directories(ServerTests PRIVATE ${CMAKE_SOURCE_DIR}/GameServer/Game/Room)
target_link_libraries(ServerTests PRIVATE ServerCore doctest::doctest)
# B3 시험용 DB 접속 정보는 로컬 Config.json 에서 읽습니다(자격증명을 시험 소스에 두지 않음).
target_compile_definitions(ServerTests PRIVATE MMO_TEST_CONFIG_PATH="${CMAKE_SOURCE_DIR}/GameServer/Config.json")

# [TD-03 · B3] 실제 MySQL 필요(라벨 db). Docker 없이 돌릴 때는 -LE db 로 명시적으로 제외.
add_test(NAME Db.ConnectionLease COMMAND ServerTests --test-suite=DbPool)
set_tests_properties(Db.ConnectionLease PROPERTIES LABELS "db" TIMEOUT 30
    FAIL_REGULAR_EXPRESSION "test cases: +0 \\|")

# 스위트 단위로 ctest 항목을 나눕니다. 라벨: unit · sabotage (+ selftest)
foreach(suite Harness SectorMath LockFreeQueue RecvBuffer Framing)
    add_test(NAME Unit.${suite} COMMAND ServerTests --test-suite=${suite})
    set_tests_properties(Unit.${suite} PROPERTIES LABELS "unit" TIMEOUT 120)
    # 실행된 시험이 0개면 실패 — doctest 는 필터에 맞는 케이스가 없어도 SUCCESS 를 반환함(빌드 실패 후 이전 exe 가 돌면 "통과"로 오판)
    set_tests_properties(Unit.${suite} PROPERTIES FAIL_REGULAR_EXPRESSION "test cases: +0 \\|")
endforeach()

# 하네스가 실패를 실제로 전달하는지 증명: 환경 변수로 무장한 스위트는 반드시 실패해야 함
add_test(NAME Sabotage.MustFail COMMAND ServerTests --test-suite=Sabotage)
set_tests_properties(Sabotage.MustFail PROPERTIES ENVIRONMENT "MMO_TEST_SABOTAGE=1" WILL_FAIL TRUE LABELS "sabotage")
