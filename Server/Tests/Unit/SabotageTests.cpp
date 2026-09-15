// [TD-03] 의도적 실패 — MMO_TEST_SABOTAGE 가 설정되면 반드시 실패합니다.
//   ctest 는 WILL_FAIL 로 등록. 환경 변수가 전달되지 않으면 이 스위트가 통과해 ctest 항목이 오히려 실패 → 설정 오류가 드러남.
#include <doctest/doctest.h>
#include "RecvBuffer.h"
#include <cstdlib>

TEST_SUITE("Sabotage")
{
    TEST_CASE("sabotage: armed suite must fail")
    {
        if (std::getenv("MMO_TEST_SABOTAGE") == nullptr)
        {
            MESSAGE("MMO_TEST_SABOTAGE not set - sabotage disarmed (pass)");
            return;
        }

        // RecvBuffer(4) 의 실제 FreeSize 는 40(= 4 x BUFFER_COUNT). 39 는 틀린 기대값.
        CHECK(RecvBuffer(4).FreeSize() == 39);
    }
}
