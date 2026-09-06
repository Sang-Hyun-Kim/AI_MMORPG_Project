# AI_MMORPG_Project

C++20 IOCP 게임 서버 · ASP.NET Core 웹 백엔드 · Unreal Engine 5 클라이언트로 구성한
**풀스택 MMORPG 학습 프로젝트**입니다. 로그인 → 캐릭터 입장 → 좌표 동기화 → 이탈까지의
왕복이 AWS EC2 위에서 실제로 성립하는 것을 목표로 만들었습니다.

> ⚠️ **학습·포트폴리오용 프로젝트입니다. 프로덕션 환경에 그대로 사용하지 마십시오.**

---

## 구성

| 디렉터리 | 내용 |
|---|---|
| `Server/GameServer` | 게임 서버 — 방·플레이어·AOI·패킷 핸들러 |
| `Server/ServerCore` | 네트워크 코어 — IOCP, 세션, `JobQueue`, DB 커넥션 풀 |
| `Server/DummyClient` | 시나리오 기반 검증용 더미 클라이언트 |
| `WebBackend` | ASP.NET Core 인증 API (계정 · 접속 티켓 발급) |
| `Shared` | 클라이언트/서버 공용 `.proto` 및 패킷 코드 제너레이터 |
| `Client/AMC1` | Unreal Engine 5 클라이언트 |
| `Infra` | MySQL · Redis · MongoDB용 `docker-compose` |

## 기술 스택

**서버** C++20 / Windows IOCP / Protobuf / vcpkg / CMake+Ninja
**백엔드** .NET 9 / ASP.NET Core / EF Core / Pomelo MySQL
**클라이언트** Unreal Engine 5 (C++)
**데이터** MySQL 8.0 · Redis · MongoDB
**배포** AWS EC2 (Windows Server 2022)

---

## 구현 하이라이트

### 스레드 격리 — `JobQueue`
게임 로직을 잡(Job) 단위로 직렬화해 실행합니다. 방 내부 상태(`_players`, `_sectors`)에
락이 하나도 없는 이유가 이것입니다.

### AOI — Uniform Grid
관심 영역 조회를 전체 순회에서 균일 격자로 교체했습니다.
좌표를 `1000` 단위 셀로 나눠 셀별 오브젝트 집합을 유지하고, 반경이 걸치는 `5×5` 셀만 조회합니다.

```
span = ⌈R / S⌉            조사 셀 = (2·span + 1)²
R = 2000, S = 1000  →  span = 2  →  25칸
```

셀로 후보를 좁힌 뒤에도 **정확한 거리 검사를 그대로 수행**하므로 반환 집합이 이전과 동일합니다.
동작 변경이 아니라 가속 구조입니다.

- 알고리즘을 별도 구현해 전체 순회와 대조한 결과, 48,000회 질의에서 **반환 집합 불일치 0건**
  (`Tools/aoi_sim.py` 로 재현 가능)
- 이동 1회당 거리 비교는 1,000명 규모 기준 `1,000회 → 7.65회`
- 실제 서버 검증은 세션 10개 상한 안에서 수행했으며 **실행 시간은 측정하지 않았습니다**

### C++20 코루틴 기반 비동기 DB
`DBAwaitable`로 워커 스레드의 MySQL 쿼리를 `co_await`로 처리합니다.

### 패킷 자동 생성
`.proto`로부터 핸들러 헤더를 생성합니다. `.cpp` 핸들러는 수동 작성합니다.

---

## 실행

### 1. 인프라
```bash
cd Infra
cp .env.example .env          # DB_PASSWORD 등을 채웁니다
docker compose up -d
```

### 2. 게임 서버
```bash
# vcpkg 의존성: protobuf, abseil, redis++, libmysql
cmake -S Server -B Server/build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build Server/build
```

### 3. 웹 백엔드
```bash
cd WebBackend
dotnet ef database update
dotnet run
```

### 4. 검증
```bash
# Redis 가 필요합니다 (티켓 발급). 기본값 tcp://127.0.0.1:6379, -redis= 로 변경 가능
DummyClient.exe -ip=127.0.0.1 -port=7777 -scenario=move   -playerid=9401 -move=1500,-2500,90
DummyClient.exe -ip=127.0.0.1 -port=7777 -scenario=verify -playerid=9401 -expect=1500,-2500,90
# 종료코드 0 = PASS
```

시나리오는 `idle` · `move` · `verify` · `stress` 네 가지입니다.
세션 수는 코드에 **10개 상한**이 걸려 있습니다(부하 시험용 도구가 아닙니다).

---

## 🔒 인증 구조

게임 서버는 로그인 티켓을 **Redis 검증이라는 단일 경로**로만 처리합니다.
문자열 패턴을 신뢰하는 우회 경로는 없으며, Redis를 사용할 수 없으면 로그인을 거부합니다.

```
C# 백엔드 /api/auth/login  ─┐
                            ├─→  Redis  Ticket:User:<ticket> = <PlayerId>
DummyClient (검증 도구)    ─┘         │
                                      ↓  게임 서버가 GET 후 DEL (1회용)
                                   C_LOGIN 검증
```

검증 도구도 예외가 아닙니다 — `DummyClient`는 C# 백엔드와 **동일한 규약으로 Redis에 티켓을 등록**한 뒤
로그인합니다. 따라서 Redis 접근 권한 없이는 로그인할 수 없습니다.

**설정 파일**: `appsettings.Development.json`의 연결 문자열은 로컬 Docker 기본값입니다.
운영 설정(`appsettings.Production.json`)은 저장소에 포함하지 않으며,
`appsettings.Production.json.example`을 복사해 채우거나 환경 변수
`ConnectionStrings__DefaultConnection`으로 주입하십시오.

## 알려진 한계

- AOI 진입/이탈 추적이 없습니다. 반경 밖에 있다가 가까워진 상대는 즉시 스폰되지 않습니다.
- 셀 크기 `1000`은 튜닝하지 않은 값입니다.
- `kCellSpan` 상수식이 절단으로 계산되어 `AOI 반경 / 셀 크기`가 나누어떨어질 때만 정확합니다.
  반경을 변경하기 전에 올림으로 고쳐야 합니다.
- 몬스터는 격자에 포함되지 않습니다.

---

## 라이선스

학습 목적으로 공개합니다. 별도 라이선스가 명시되지 않은 부분은 모든 권리를 유보합니다.
