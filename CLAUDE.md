# Role: 풀스택 MMORPG 프로젝트(C++20, C#, UE5) 릴레이 개발 에이전트

## Core Directory
- `Docs/`: 마스터 설계(`AI_MMORPG_Master_Design_V9.md`) 및 코드 명세서(`Code_Specification.md`)
- `Server/`: 게임 서버 코어 (C++20, IOCP)
- `Shared/`: 클라이언트/서버 공용 자원 (Protobuf `.proto`)

## Handoff & Log Protocol (MANDATORY)
1. **시작**: `Docs/AI_HANDOFF.md`를 읽고 미해결 과제와 컨텍스트를 파악할 것.
2. **종료**: `Docs/AI_HANDOFF.md`를 다음 작업자를 위해 갱신할 것.
3. **로그**: 작업 종료 시 `Docs/ProgressLogs/YYYY-MM-DD_ProgressLog.md`에 진행 내역과 **트러블 슈팅(에러/해결법)**을 누적 기록할 것.

## Critical Rules
1. **패킷 룰**: `Shared/Scripts/GenPackets.bat` 실행 시 `.h`만 갱신됩니다. `.cpp` 핸들러는 템플릿으로 덮어쓰지 말고 반드시 **수동 작성**하세요.
2. **명세 갱신**: 코드 수정 시 `Docs/Code_Specification.md`를 반드시 최신화하세요.
3. **이식 기록**: 레거시(C++17) 구조 변경 시 `Docs/Cpp20_Migration_Comparison.md`에 기록하세요.
4. **클래스 명세서 필수 작성 (NEW Rule)**: 새로 작성되는 클래스 파일들(C++, C# 모두)에 대해 반드시 구현 명세(Class, Method, variables)가 작성되어야 합니다. 이 명세는 추후 복습 및 프로젝트 이해를 돕기 위해 기존 클래스와 새로 추가될 내용을 모두 포함해야 합니다. 명세서 파일은 `Docs/Code_Specification.md`에 기록하며, 이는 `.gitignore` 규칙에 의해 원격 저장소에 포함되지 않습니다.
5. 과거 Reference 코드는 절대 수정하지 말고 읽기 용도로만 사용하세요.