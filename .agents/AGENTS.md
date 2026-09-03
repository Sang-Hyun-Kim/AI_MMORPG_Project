# Role: 풀스택 MMORPG 프로젝트(C++20, C#, UE5) 릴레이 개발 에이전트

## Core Directory
- `Docs/`: 마스터 설계(`AI_MMORPG_Master_Design_V9.md`) 및 코드 명세서(`Code_Specification.md`)
- `Server/`: 게임 서버 코어 (C++20, Windows IOCP)
- `Shared/`: 클라이언트/서버 공용 자원 (Protobuf `.proto` 및 코드 제너레이터)
- `Client/AMC1/`: 언리얼 엔진 5.8 클라이언트 프로젝트
- `Reference/`: 과거 C++17 레퍼런스 프로젝트 (절대 수정 금지, 읽기 전용)

> ⚠️ **본 문서는 루트 `CLAUDE.md`(Claude CLI용 지침)와 내용이 완전히 동기화되어 있어야 합니다.**
> 한쪽을 수정하면 반드시 다른 쪽도 동일하게 갱신하세요. (최종 동기화: 2026-09-03)

## Handoff & Log Protocol (MANDATORY)
1. **시작**: `Docs/AI_HANDOFF.md`를 읽고 미해결 과제와 컨텍스트를 파악할 것.
2. **종료**: `Docs/AI_HANDOFF.md`를 다음 작업자를 위해 갱신할 것.
3. **로그**: 작업 종료 시 `Docs/ProgressLogs/YYYY-MM-DD_ProgressLog.md`에 진행 내역과 **트러블 슈팅(에러/해결법)**을 누적 기록할 것.

## 🚨 Critical Architecture Warnings (인수인계 경고)
- **버퍼 접근(Buffer Access) 관련 경고**: C++17 환경(과거 레퍼런스)에서는 바이트 단위로 읽어왔으나, C++20으로 이식하며 안전성을 위해 버퍼를 `std::span` 및 `data()` 함수로 읽어오는 모던 C++ 방식을 채택했습니다. 
- **주의사항**: 서버 쪽 `std::vector`는 `.data()`를 사용하지만, 클라이언트 쪽 언리얼 엔진 `TArray`는 `.GetData()`를 사용해야 합니다. 템플릿(`PacketHandler.h`) 코드에서 매크로 분기(`UE_BUILD_DEBUG...`)를 통해 두 환경이 각각 맞는 함수를 호출하도록 조치했으니, 추후 이 부분을 덮어쓰거나 훼손하지 않도록 주의하세요.
- **서버 로그인 Redis 임시 우회 (개발용)**: 현재 `ServerPacketHandler.cpp`의 `Handle_C_LOGIN` 함수에는 로컬 Redis가 켜져있지 않을 경우를 대비해 **무조건 `S_LOGIN(success=true)`를 반환하는 백도어(우회 코드)**가 심어져 있습니다. 추후 Production 환경이나 정식 로그인 시스템 개발 시 반드시 해당 우회 로직을 삭제하고 정상적인 Redis 검증 로직으로 복원해야 합니다!
- **클라이언트 네트워크 스레드 경계**: `FNetworkWorker::Run()`(백그라운드 워커)에서 수신한 패킷은 반드시 `AsyncTask(ENamedThreads::GameThread, ...)`로 디스패치하고, `TArray<uint8> PacketCopy`로 독립 복사한 뒤 `MoveTemp`로 넘겨야 합니다. 이 구조를 제거하면 `SpawnActor` 호출 시 `check(IsInGameThread())` assertion으로 100% 크래시합니다. (2026-09-03 적용)
- **`ClientPacketHandler::GGameInstance`**: Raw Pointer가 아닌 `TWeakObjectPtr<UAMC1GameInstance>`이며 `Shutdown()`에서 `nullptr`로 정리됩니다. GC 미인식/댕글링 방지를 위한 조치이므로 Raw Pointer로 되돌리지 마세요. (9/5 데모 이후 DI 패턴 또는 이벤트 큐로 리팩토링 예정 — 추적 대상)

## Critical Rules
1. **패킷 룰**: `Shared/Scripts/GenPackets.bat` 실행 시 `.h`만 갱신됩니다. `.cpp` 핸들러는 템플릿으로 덮어쓰지 말고 반드시 **수동 작성**하세요.
2. **명세 갱신**: 코드 수정 시 `Docs/Code_Specification.md`를 반드시 최신화하세요.
3. **이식 기록**: 레거시(C++17) 구조 변경 시 `Docs/Cpp20_Migration_Comparison.md`에 기록하세요.
4. **클래스 명세서 필수 작성 (NEW Rule)**: 새로 작성되는 클래스 파일들(C++, C# 모두)에 대해 반드시 구현 명세(Class, Method, variables)가 작성되어야 합니다. 이 명세는 추후 복습 및 프로젝트 이해를 돕기 위해 기존 클래스와 새로 추가될 내용을 모두 포함해야 합니다. 명세서 파일은 `Docs/Code_Specification.md`에 기록하며, 이는 `.gitignore` 규칙에 의해 원격 저장소에 포함되지 않습니다.
5. **접근 금지**: 과거 Reference 코드는 절대 수정하지 말고 분석/읽기 용도로만 사용하세요.
6. **진행 로그 상세 작성 규칙 (NEW Rule)**: `ProgressLog.md` 기록 시 절대 단순 요약(Bullet point)만 남기지 마세요.
   - **기능 추가**: 추가/수정된 클래스, 메서드, 데이터 플로우를 명확히 기술할 것.
   - **버그 수정**: 기존 코드의 문제점(Before), 개선된 코드(After)의 차이점과 근본 원인(Root Cause)을 반드시 포함할 것.
   - **아키텍처 변경**: `main` 루프 제어 방식 변경, 스레드 모델 변경 등은 추후 인수인계자(다른 AI)가 시스템을 망가뜨리지 않도록 그 "변경 사유와 유지 지침"을 명확히 경고할 것.
7. **[방안 1] 무단 삭제 원천 금지**: 앞으로 컴파일 또는 링킹 에러가 발생하더라도, 기존에 멀쩡히 존재하던 코드를 임의로 삭제하거나 주석 처리하는 행위를 절대 금지함.
8. **[방안 2] 의존성(Dependency) 최우선 점검**: 헤더 누락이나 심볼 찾기 실패 에러가 나면, 무조건 `vcpkg list`, `CMakeLists.txt`의 `find_package`, 언리얼 `Build.cs` 모듈 설정을 가장 먼저 검증할 것.
9. **[방안 3] 코드 의도 분석 및 사전 승인(Ask First)**: 설계 문서와 현재 코드 간의 괴리가 발견될 경우, 자의적인 해석으로 코드를 재단하지 않고 반드시 유저에게 "이 코드가 설계 문서와 다르게 남아 있는데 지워도 될까요?" 라고 먼저 보고하고 질문(Ask) 할 것.
10. **[방안 4] Unreal MCP & VibeUE 툴셋 적극 활용 및 한계 판단 (중요)**: Unreal Engine 5.8의 네이티브 MCP(`ToolsetRegistry`)와 VibeUE 플러그인은 블루프린트 노드 와이어링(`BlueprintTools`), 재질(`MaterialTools`), 씬 구성 등 시각적 스크립팅을 자동화하는 강력한 툴셋을 제공합니다. 가급적 `http://127.0.0.1:8000/mcp`의 `list_toolsets` 및 `describe_toolset`을 호출해 가용한 도구를 1순위로 탐색하여 에디터 자동화를 수행할 것. **단, 분석 결과 현재 MCP 툴셋이나 파이썬 API로 도저히 지원되지 않는 기능(근본적 한계)이라고 판단될 경우, 억지 우회 코드를 남발하지 말고 유저에게 명확한 사유를 보고한 뒤 수동 작업을 정중히 요청하는 판단력(Depth)을 발휘할 것.**
11. **[방안 5] AWS EC2 비용 방어 필수 지침 (MANDATORY)**: 매 테스트 종료 및 작업 세션 종료 시, 사용하지 않는 AWS EC2 인스턴스를 반드시 **'중지(Stop)'** 하도록 사용자에게 적극 상기시킬 것 (온디맨드 종량제 컴퓨팅 요금 0원 방어).
