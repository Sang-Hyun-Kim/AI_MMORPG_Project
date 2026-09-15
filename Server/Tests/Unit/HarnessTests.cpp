// [TD-03] 하네스 자체 점검 — doctest 가 실패를 집계하고 should_fail 이 이를 뒤집는지 확인합니다.
#include <doctest/doctest.h>

TEST_SUITE("Harness")
{
    TEST_CASE("harness: passing check is counted")
    {
        CHECK(1 + 1 == 2);
    }

    // 실패가 "실패"로 집계되어야 should_fail 이 통과로 뒤집힘 — 실패 전달 경로의 음성 대조군
    TEST_CASE("harness: should_fail flips a real failure" * doctest::should_fail())
    {
        CHECK(1 == 2);
    }
}
