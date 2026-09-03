# 취약점 및 기술 부채 대장 (Vulnerability & Tech Debt Register)

> **작성**: 2026-09-04 · Claude Opus 5 (Claude Code CLI)
> **기준 커밋**: `c40b029` (master, Phase 4 P3 병합 완료)
> **목적**: 데모 이후 / Production 이행 전에 반드시 처리해야 할 항목을
> 심각도·근거·재현 방법·조치 방향과 함께 한 곳에 모읍니다.
>
> ⚠️ 이 문서는 **미해결 항목만** 다룹니다. 이미 조치된 결함의 경위는
> `ProgressLogs/`와 `ProjectAnalysis_004_2026-09-03.md`를 참조하십시오.

---

## 0. 심각도 기준

| 등급 | 정의 |
|---|---|
| **S1 · 치명** | 원격에서 인증 없이 서버를 정지시키거나 데이터를 파괴할 수 있음 |
| **S2 · 높음** | 인증 우회, 데이터 유출, 데이터 무결성 훼손 가능 |
| **S3 · 중간** | 특정 조건에서 오작동. 운영 중 발견 시 서비스 품질 저하 |
| **S4 · 낮음** | 기능에 영향 없으나 유지보수·디버깅을 저해 |

---

## 1. 요약 — 미해결 항목 일람

| ID | 항목 | 등급 | 영역 | 처리 시점 |
|---|---|---|---|---|
| **V1** | 비밀번호 평문 저장·비교 | **S2** | C# | Production 전 **필수** |
| **V2** | 로그인 테스트 백도어 존치 | **S2** | C++ | Production 전 **필수** |
| **V3** | SQL 문자열 조립 (Prepared Statement 미도입) | **S2** | C++ | 데모 후 |
| **V4** | 인증 API 평문 HTTP (TLS 없음) | **S2** | C# / UE | Production 전 |
| **V5** | 로그인 시도 횟수 제한 없음 | S3 | C# | Production 전 |
| **V6** | 게임 서버 이동 좌표 검증 부재 | S3 | C++ | 데모 후 |
| **V7** | EF Migration 스냅샷 불일치 | S3 | C# | 다음 마이그레이션 전 |
| **V8** | `RegisterSend` 경합 창 (마지막 패킷 유실 가능) | S3 | C++ | 데모 후 |
| **V9** | `DBAwaitable(dbJob, nullptr)` 데이터 레이스 | S3 | C++ | 데모 후 |
| **V10** | UE 재연결·타임아웃 처리 부재 | S3 | UE | 데모 후 |
| **V11** | 멀티스레드 `std::cout` 인터리빙 | S4 | C++ | 데모 후 |
| **V12** | DB 자격 증명 평문(`root`/`root`) | **S2** | 전체 | Production 전 |
| **V13** | 계정당 캐릭터 1개 전제 | S4 | 설계 | 캐릭터 선택 도입 시 |
| **V14** | 맵/존 ID 기획 미착수 | S4 | 기획 | 차기 |

---

## 2. Production 전 필수 (S2)

### V1 · 비밀번호 평문 저장 및 비교

**위치**: `WebBackend/Controllers/AuthController.cs`, `WebBackend/Models/Account.cs`

```csharp
// In a real application, passwords should be hashed and compared securely.
// For portfolio purpose, we do a simple check.
if (account == null || account.Password != request.Password)
```

**영향** — DB가 유출되면 모든 계정의 비밀번호가 즉시 노출됩니다. 사용자가 다른
서비스와 비밀번호를 공유하고 있다면 피해가 이 프로젝트 밖으로 번집니다.

**근거** — `Account.Password`가 `varchar(100)` 평문이며 코드 주석에도 명시되어 있습니다.

**조치 방향**
1. `Microsoft.AspNetCore.Identity`의 `PasswordHasher<T>` 또는 BCrypt 도입
2. `Account`에 `PasswordHash` 컬럼 추가 후 기존 컬럼 폐기
3. 로그인 시 `VerifyHashedPassword`로 비교
4. `seed_accounts.sql`도 해시값을 넣도록 변경 (또는 최초 로그인 시 마이그레이션)

**참고** — 현재는 포트폴리오 데모 범위이므로 의도적으로 유지 중입니다.

---

### V2 · 로그인 테스트 백도어 존치

**위치**: `Server/GameServer/Packet/ServerPacketHandler.cpp` `Handle_C_LOGIN`

```cpp
if (lowerTicket.starts_with("dummy"))
{
    uint64 backdoorPlayerId = ExtractTrailingNumber(lowerTicket, 1);
    GRedisManager->GetRedis()->set(key, std::to_string(backdoorPlayerId));
}
```

**영향** — **인증 완전 우회.** `dummy_123` 티켓만 보내면 누구나 `PlayerId 123` 캐릭터로
접속할 수 있습니다. 서버가 스스로 티켓을 만들어 스스로 검증하므로
C# 백엔드도 계정 검증도 필요 없습니다.

**재현**
```
DummyClient.exe -ip=<서버IP> -port=7777 -scenario=idle -playerid=<임의의ID>
```

**존치 사유 (의도적)** — 지우면 C# 백엔드가 유일한 진입 수단이 되어, 데모 당일
C#이 기동하지 못하면 게임에 들어갈 방법이 사라집니다. 폴백을 유지하기 위한 결정이며
`CLAUDE.md`의 "Production 전 반드시 삭제" 지침은 그대로 유효합니다.

**조치 방향** — Production 이행 시 해당 `if` 블록 전체와 Redis 오프라인 우회 분기를
함께 삭제하고, `ServerCore`에 `#ifdef _DEBUG` 가드를 두는 방식도 검토하십시오.
`UAMC1GameInstance::bBypassAuthServer`도 함께 제거해야 합니다.

---

### V3 · SQL 문자열 조립

**위치**: `Server/GameServer/GameSession.cpp`, `Server/GameServer/Game/Room/GameRoom.cpp`

```cpp
const std::string selectQuery =
    "SELECT Name, Level, Gold, PosX, PosY, PosZ FROM Player WHERE PlayerId = " +
    std::to_string(playerId);
```

**현재 방어 수준**
- `playerId`는 `Handle_C_LOGIN`의 `ParseUInt64`(`from_chars`)를 통과한 **순수 숫자**입니다.
  문자열 전체가 숫자일 때만 통과하므로 이 경로로는 주입이 불가능합니다.
- 문자열 값(`Name`)은 `conn->EscapeString()`(`mysql_real_escape_string`)을 거칩니다.

**남은 위험** — 방어가 **호출 지점의 규율에 의존**합니다. 향후 다른 개발자가
검증되지 않은 문자열을 쿼리에 이어 붙이면 즉시 주입 경로가 생깁니다.
채팅 내용, 캐릭터명 등 사용자 입력이 늘어날수록 위험이 커집니다.

**조치 방향** — `mysql_stmt_prepare` / `mysql_stmt_bind_param` 기반
`DBStatement` 클래스를 `ServerCore`에 추가하고 기존 쿼리를 이관합니다.
`DBResult`와 같은 RAII 패턴을 적용해 `mysql_stmt_close`를 소멸자에 맡기십시오.
**예상 공수 3시간 이상**이라 데모 후 과제로 미뤘습니다.

---

### V4 · 인증 API 평문 HTTP

**위치**: `WebBackend/Program.cs`, `Client/AMC1/.../AMC1GameInstance.cpp`

```csharp
if (app.Environment.IsDevelopment())
{
    app.UseHttpsRedirection();   // 운영에서는 적용하지 않음
}
```

**영향** — 계정명·비밀번호가 **평문으로 네트워크를 통과**합니다.
동일 네트워크의 공격자가 스니핑으로 자격 증명을 획득할 수 있고,
발급된 티켓을 가로채면 그 캐릭터로 접속할 수 있습니다.

**현재 판단** — EC2에 TLS 인증서가 없고 자체 서명 인증서는 UE의 HTTP 모듈이 거부하므로,
데모 범위에서는 HTTP만 사용합니다. **데모는 시연자 본인의 네트워크에서 수행**되므로
위험이 제한적입니다.

**조치 방향**
1. 도메인 확보 후 Let's Encrypt 인증서 발급
2. `UseHttpsRedirection()`을 전 환경에서 활성화
3. UE의 `AuthBaseUrl` 기본값을 `https://`로 변경
4. 또는 API Gateway / ALB에서 TLS 종단 처리

---

### V12 · DB 자격 증명 평문

**위치**: `Server/GameServer/Config.json`, `WebBackend/appsettings*.json`

```json
"User": "root", "Password": "root"
"DefaultConnection": "...User Id=root;Password=root;"
```

**영향** — 저장소에 커밋되어 있으며(`Config.json`은 배포 번들에도 포함),
`root` 계정을 그대로 사용합니다. 서버 침해 시 DB 전권이 넘어갑니다.

**조치 방향**
1. 게임 서버 전용 계정을 만들고 `Player`/`Mailbox`에 대한 최소 권한만 부여
2. 자격 증명을 환경 변수 또는 AWS Secrets Manager로 이전
3. `Config.json`·`appsettings.Production.json`을 `.gitignore`에 추가하고
   템플릿(`Config.sample.json`)만 커밋

---

## 3. 중간 심각도 (S3)

### V5 · 로그인 시도 횟수 제한 없음

**위치**: `WebBackend/Controllers/AuthController.cs`

`/api/auth/login`에 속도 제한이 없어 무차별 대입 공격이 가능합니다.
V1(평문 비밀번호)과 겹치면 위험이 커집니다.

**조치** — ASP.NET Core `RateLimiter` 미들웨어 또는 계정별 실패 횟수 카운터(Redis).

---

### V6 · 이동 좌표 서버 검증 부재

**위치**: `Server/GameServer/Game/Room/GameRoom.cpp` `HandleMove`

```cpp
player->GetPosInfo()->CopyFrom(pkt.posinfo());   // 클라이언트 값을 그대로 신뢰
```

**영향** — 클라이언트가 보낸 좌표를 검증 없이 반영합니다. 조작된 클라이언트가
순간이동하거나 맵 밖으로 나갈 수 있으며, 그 좌표가 그대로 DB에 저장됩니다.

**조치 방향** — 직전 좌표와의 거리 / 경과 시간으로 최대 이동 속도를 검증하고,
초과 시 서버 좌표로 되돌리는 보정 패킷을 보냅니다.
**데모 목표가 연결성이므로 이번 범위에서 제외했습니다.**

---

### V7 · EF Migration 스냅샷 불일치

**위치**: `WebBackend/Migrations/*.Designer.cs`, `ApplicationDbContextModelSnapshot.cs`

2026-09-04에 `Init` 마이그레이션에서 `Player` 생성 블록을 제거하고
`ExcludeFromMigrations()`를 적용했으나, **Designer와 Snapshot에는 구 `Player` 모델
(`PlayerId`/`PlayerName`/`AccountId` 3컬럼)이 그대로 남아 있습니다.**

**영향** — 지금은 마이그레이션을 추가하지 않아 무해합니다. 그러나 향후
`dotnet ef migrations add`를 실행하면 스냅샷과 현재 모델의 차이 때문에
**예상치 못한 DDL(특히 `Player`에 대한 `DropTable`/`AlterColumn`)이 생성될 수 있습니다.**

**조치**
1. 새 마이그레이션 추가 시 **생성된 `Up()`을 반드시 눈으로 검토**하고
   `Player`에 대한 조작이 있으면 삭제
2. 여유가 되면 마이그레이션을 초기화(`Migrations/` 삭제 후 재생성)하되,
   운영 DB의 `__EFMigrationsHistory`와의 정합성을 먼저 확인

---

### V8 · `Session::RegisterSend` 경합 창

**위치**: `Server/ServerCore/Session.cpp`

**내용** — 송신 등록 시점의 경합으로, 큐에 쌓인 마지막 패킷이 **다음 `Send()` 호출이
올 때까지 무기한 지연**될 수 있습니다. 브로드캐스트가 계속 흐르는 룸에서는 즉시
치유되지만, **세션의 마지막 패킷(종료 직전 `S_DESPAWN`, 저빈도 1:1 응답)은 영영
전송되지 않을 수 있습니다.**

**이력** — 초기에 "자연 치유 구조, 이슈 없음"으로 종결했다가 2026-09-03 재평가에서
과대 판정이었음이 확인되어 **미해결로 하향**되었습니다.

**조치** — `[M1]` JobQueue 배선 작업과 함께 재검토 (`M1_JobQueue_Wiring_Decision.md` 참조).

---

### V9 · `DBAwaitable(dbJob, nullptr)` 데이터 레이스

**위치**: `Server/GameServer/GameSession.cpp` `LoadPlayerTask`

```cpp
co_await DBAwaitable(dbJob, nullptr);   // resumeQueue가 null
```

`resumeQueue`가 `nullptr`이면 **DB 워커 스레드에서 코루틴이 직접 재개**됩니다.
그 이후 코드가 `Player` 생성·`Send`·`GameRoom::Enter` 큐잉을 수행하므로,
세션 상태가 게임 스레드와 동시에 접근될 여지가 있습니다.

**현재 완화** — `GameRoom::Enter`는 `DoAsync`로 룸 큐에 넣으므로 룸 상태는 안전합니다.
문제가 되는 것은 `session->SetPlayer()`와 `Send()` 구간입니다.

**조치** — 세션 전용 `JobQueue`를 도입해 `resumeQueue`로 넘기는 것이 정석이나,
범위가 커서 데모 후로 미뤘습니다. `DBAwaitable.h` 주석에도 경고가 있습니다.

---

### V10 · UE 재연결·타임아웃 처리 부재

**위치**: `Client/AMC1/Source/AMC1/AMC1GameInstance.cpp` `ConnectToServer`

접속 실패나 도중 끊김에 대한 재시도 로직이 없습니다. WAN 환경에서 한 번 끊기면
**PIE를 재시작해야 복구**됩니다.

**조치** — 지수 백오프 재연결과 하트비트 타임아웃 감지. 데모에서는
리허설 때 수동 재시작을 연습하는 것으로 대응합니다.

---

## 4. 낮은 심각도 (S4)

### V11 · 멀티스레드 `std::cout` 인터리빙

**위치**: `Server/GameServer/Game/Room/GameRoom.cpp`, `GameSession.cpp`

**발견** — 2026-09-04 3세션 동시 테스트 중 실제 관측:

```
[GameRoom] DB Save: no change. PlayerId=Player Player_8001 (PlayerId=8001) DB Save Complete. Pos=(0, 0, 80030) Gold=
```

`Pos=(0, 0, 80030)`은 **존재하지 않는 값**입니다. `(0,0,0)`의 마지막 `0`과
다른 스레드가 출력한 `8003`이 이어 붙은 것입니다.

**원인** — `std::cout`은 개별 `<<` 연산에 대해서만 스레드 안전하며,
**한 문장 전체의 원자성은 보장하지 않습니다.** DB 워커 스레드와 GameRoom
JobQueue 스레드가 동시에 출력하면 섞입니다.

**영향** — 기능 결함은 아니지만 **디버깅을 적극적으로 오도합니다.**
유령 값을 보고 좌표 계산 버그를 의심하며 시간을 쓰게 됩니다.

**조치 방향** — 한 줄을 `std::ostringstream`으로 조립한 뒤
`std::cout << oss.str()` 한 번으로 출력. 완전한 보장이 필요하면 뮤텍스 로거 도입.

---

### V13 · 계정당 캐릭터 1개 전제

**위치**: `WebBackend/Controllers/AuthController.cs`

```csharp
var player = await _context.Players.FirstOrDefaultAsync(p => p.AccountId == account.AccountId);
```

계정의 **첫 번째** 캐릭터를 무조건 선택합니다. 캐릭터가 여러 개면 나머지는 접근 불가입니다.

**설계 전제** — 현재 C++ 게임 서버는 계정 개념을 모르고 `PlayerId`만 다룹니다.
캐릭터 선택 UI를 도입하면 **"계정의 여러 캐릭터 중 무엇을 고를지"** 를 클라이언트가
결정해야 하므로, 이 관심사 분리 전제를 재검토해야 합니다.

**조치 방향** — 캐릭터 목록 API(`GET /api/characters`) + 선택 후 티켓 발급,
또는 `C_SELECT_CHARACTER` 패킷 도입.

---

### V14 · 맵/존 ID 기획 미착수

**현황** — `Player` 테이블에 맵/존 ID 컬럼이 **존재하지 않으며 기획 자체가 미착수**입니다.
현재는 모든 플레이어가 단일 `GameRoom(1)`에 입장합니다.

> ⚠️ **혼동 주의**: `Player.Level`은 **캐릭터 레벨**이며 맵과 무관합니다.
> (`Exp`·`Hp`와 나란한 성장 스탯입니다)

**조치 방향** — 데이터시트로 맵을 기획한 뒤 `Player.MapId` 추가,
`GameRoomManager`가 맵별 룸을 관리하도록 확장, 존 이동 패킷 설계.

---

## 5. 데모 후 착수 금지 해제 목록 (참고)

아래는 취약점이 아니라 **의도적으로 미룬 개선 과제**입니다.
`AI_HANDOFF.md`에서 "데모 전 착수 금지"로 합의된 항목이며,
데모가 끝나면 이 대장의 V3·V6·V8·V9와 함께 우선순위를 재산정하십시오.

| ID | 항목 | 성격 |
|---|---|---|
| M1 | JobQueue 타임슬라이스 배선 | 구조 (문서는 완료형인데 코드는 미배선 — 문서 정정 완료) |
| N1 | 메모리 풀 | 성능 |
| N2 | 패킷 배칭 | 성능 |
| N3 | AOI 그리드 | 성능 |
| O2 O3 O7 | 객체지향 리팩토링 | 유지보수성 |

---

## 6. 갱신 이력

| 날짜 | 내용 |
|---|---|
| 2026-09-04 | 최초 작성. V1~V14 등록. V11은 당일 3세션 테스트 중 실제 관측하여 추가 |
