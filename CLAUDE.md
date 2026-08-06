# Role & Workflow (Relay Worker)
당신은 Antigravity IDE 에이전트와 함께 풀스택 MMORPG(C++20 서버, C# 백엔드, UE5)를 개발하는 AI 에이전트입니다. 토큰 제한이나 작업 중단 시 상대방이 즉시 이어서 작업할 수 있도록 '릴레이(Handoff)' 방식으로 일해야 합니다.

# Project Directory Structure (전체 지형도)
작업 시 항상 아래의 폴더 구조와 목적을 준수하여 파일을 배치하세요. 임의로 최상단에 새 폴더를 생성하지 마세요.
- `Docs/` : 마스터 설계 문서(`AI_MMORPG_Master_Design_V9.md`) 및 인수인계 파일(`AI_HANDOFF.md`)
- `AI_Skills/` : 에이전트 공용(Shared) 및 개별(Claude) 디버깅/유틸리티 스크립트
- `Infra/` : Docker Compose (MySQL, Redis, MongoDB) 및 DB 초기화 스크립트
- `Shared/` : 클라이언트/서버 공용 자원 (Protobuf `.proto` 파일 및 코드 제너레이터)
- `Server/` : 게임 서버 코어 (C++20, Windows IOCP, Memory Pool, Lock-free Queue)
- `WebBackend/` : 웹 API 및 아웃게임 로직 (C# ASP.NET Core)
- `Client/` : Unreal Engine 5 클라이언트 (C++ / BP)
- `Tools/` : QA 및 헤드리스 더미 봇 (스트레스 테스트용)
- `MMORPG_Project.code-workspace` : 모노레포 통합 워크스페이스 설정 파일 (빌드 파일 제외 룰 포함)

# Reference Document Rules
- [마스터 아키텍처 기준]: `Docs/AI_MMORPG_Master_Design_V9.md` (C++ 멀티스레딩, IOCP, 스레드 격리, DB 룰 등 결정 시 최우선 참조)
- [현재 상태 동기화]: `Docs/AI_HANDOFF.md`

# Handoff Protocol (MANDATORY)
1. 세션 시작 시: 반드시 `Docs/AI_HANDOFF.md`를 먼저 읽고(cat/read), 현재 진행 중이던 작업, 남은 과제, 에러를 파악한 후 작업을 시작하세요.
2. 세션 종료/중단 시: 작업을 마치거나 토큰 한계로 중단할 경우, **반드시** `Docs/AI_HANDOFF.md`를 업데이트하여 다음 에이전트(Antigravity)가 무엇을 이어서 해야 하는지 명확히 기록하세요.

# Core Constraints
- C++ 코딩 시 Modern C++20 표준을 준수하며, 메모리 풀과 락프리 큐 기반의 아키텍처를 절대 훼손하지 마세요.