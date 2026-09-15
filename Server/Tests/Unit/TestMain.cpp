// [TD-03] ServerTests 진입점. doctest 실행 전에 로거와 소켓 계층을 준비합니다.
#define DOCTEST_CONFIG_IMPLEMENT
// ServerCore PCH 의 Windows.h min/max 매크로가 doctest 구현부의 std::max(...) 를 깨뜨림(C2589) — 이 파일에서만 해제
#undef min
#undef max
#include <doctest/doctest.h>
#include "SocketUtils.h"

int main(int argc, char** argv)
{
    Logger::Init({ .programName = "ServerTests", .async = false });  // 시험 로그 판정을 위해 동기 기록
    Logger::SetThreadName("MAIN");

    // Session() 이 WSASocket 을 부르므로 WSAStartup 선행.
    // ⚠️ CoreGlobal.obj 가 링크되면 전역 GCoreGlobal 도 Init/Clear 를 부름 — WSAStartup 은 참조 계수라 짝만 맞으면 무해.
    SocketUtils::Init();

    doctest::Context context(argc, argv);
    const int result = context.run();

    SocketUtils::Clear();
    Logger::Flush();
    return result;
}
