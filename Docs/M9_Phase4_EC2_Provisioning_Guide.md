# AWS EC2 프로비저닝 가이드 — Phase 4 배포 절차서

> **작성**: 2026-09-04 · Claude Opus 5 (Claude Code CLI)
> **대상**: AWS EC2 Windows Server 2022 단일 인스턴스 (Option A)
> **선행 문서**: `M9_Phase4_AWS_Deployment_Plan.md` · `Phase4_Work_Plan_2026-09-03.md`
> **총 예상 소요**: 2~4시간 (Docker Desktop 설치·재부팅 구간이 가장 깁니다)

---

## 0. 이 문서를 읽기 전에

### 0-1. 무엇을 배포하는가

단일 EC2 인스턴스 안에 네 가지가 함께 뜹니다.

| 구성 요소 | 포트 | 외부 개방 | 역할 |
|---|---|---|---|
| **C++ GameServer** | 7777 | ✅ 개방 | IOCP 게임 서버. UE 클라이언트가 TCP로 접속 |
| **C# WebBackend** | 5000 | ✅ 개방 | 로그인 API. 티켓을 발급해 Redis에 저장 |
| **MySQL** (Docker) | 3307 | ❌ **차단** | 게임 DB. 루프백으로만 접근 |
| **Redis** (Docker) | 6379 | ❌ **차단** | 티켓 전달 매개. 루프백으로만 접근 |

**MySQL·Redis를 외부에 열지 마십시오.** 두 서비스는 같은 인스턴스 안에서만 통신하며,
외부 개방은 공격면만 넓힙니다. 게임 서버와 백엔드 모두 `127.0.0.1`로 접속하도록 설정되어 있습니다.

### 0-2. 데이터 흐름 (설정을 어디에 맞춰야 하는지 이해하기 위해)

```
UE 클라이언트                EC2 인스턴스
    │
    │ ① POST /api/auth/login (HTTP 5000)
    ├──────────────────────────▶ C# WebBackend
    │                              │ ② Account 검증, Player 행 확보
    │                              │ ③ Redis에 Ticket:User:<t> → PlayerId 저장
    │ ④ { ticket, playerId }       │        │
    ◀──────────────────────────────┘        │ (루프백 6379)
    │                                       ▼
    │ ⑤ TCP 접속 + C_LOGIN(ticket)       [Redis]
    ├──────────────────────────▶ C++ GameServer
    │                              │ ⑥ Redis GET/DEL → PlayerId 확정
    │                              │ ⑦ MySQL SELECT → 저장된 좌표·골드 로드
    │ ⑧ S_ENTER_GAME(좌표 포함)     │        │ (루프백 3307)
    ◀──────────────────────────────┘        ▼
    │ ⑨ 저장된 자리에서 재개              [MySQL]
```

**핵심**: ③과 ⑥이 **같은 Redis**를, ⑦이 C#과 **같은 MySQL**을 봐야 합니다.
설정이 어긋나면 로그인은 성공하는데 게임 접속에서 강퇴되는, 원인을 찾기 어려운 증상이 납니다.

---

## 1. 인스턴스 생성

### 1-1. AMI 및 인스턴스 유형

| 항목 | 값 | 비고 |
|---|---|---|
| AMI | **Microsoft Windows Server 2022 Base** | Linux 불가 — 서버 코어가 Windows IOCP 기반입니다 |
| 인스턴스 유형 | **t3.medium** (2 vCPU / 4 GiB) | Docker Desktop + 게임 서버 + 백엔드를 함께 돌리기 위한 최소 사양 |
| 스토리지 | **gp3 50 GiB** | Windows Server 기본 30GiB는 Docker 이미지까지 담기에 빠듯합니다 |
| 키 페어 | 신규 생성 후 `.pem` **안전 보관** | RDP 비밀번호 복호화에 필요. 분실하면 접속 불가 |

> **비용 참고**: `t3.medium` 온디맨드는 서울 리전 기준 시간당 약 $0.052입니다.
> 하루 8시간 사용 시 월 $12 수준이나, **끄지 않으면 월 $37**이 됩니다.
> §9의 중지 절차를 반드시 지키십시오.

### 1-2. RDP 접속 준비

1. 인스턴스 생성 후 **상태 검사 2/2 통과**까지 대기 (약 3~5분)
2. 인스턴스 선택 → **연결** → **RDP 클라이언트** 탭
3. **암호 가져오기** → 키 페어 `.pem` 업로드 → 암호 복호화
4. 표시된 사용자 이름(`Administrator`)과 암호로 RDP 접속

---

## 2. 보안 그룹 — 인바운드 규칙

인스턴스의 보안 그룹에 다음 4개 규칙만 둡니다.

| 유형 | 프로토콜 | 포트 | 소스 | 용도 |
|---|---|---|---|---|
| 사용자 지정 TCP | TCP | **7777** | `0.0.0.0/0` | 게임 서버 (데모용 전체 개방) |
| 사용자 지정 TCP | TCP | **5000** | `0.0.0.0/0` | C# 로그인 API |
| RDP | TCP | 3389 | **내 IP** | 원격 데스크톱 — 반드시 본인 IP로 제한 |
| — | — | ~~3307~~ ~~6379~~ | **규칙 없음** | MySQL·Redis는 열지 않습니다 |

> ⚠️ **RDP를 `0.0.0.0/0`으로 열지 마십시오.** 퍼블릭 IP의 3389 포트는 자동화된
> 무차별 대입 공격의 상시 표적입니다. 카페 등에서 IP가 바뀌면 그때 규칙을 수정하십시오.

> **참고**: `M9_Phase4_AWS_Deployment_Plan.md`는 5000·5001 두 포트를 열도록 적고 있으나,
> 실제 백엔드는 HTTP 단일 포트(5000)만 사용합니다. HTTPS(5001)는 인증서가 없어
> 사용하지 않으므로 열지 않습니다. (계획서 문구는 이 가이드 기준으로 정정 대상)

---

## 3. Elastic IP 할당

**반드시 수행하십시오.** 하지 않으면 인스턴스를 중지했다 켤 때마다 퍼블릭 IP가 바뀌고,
그때마다 UE 클라이언트 설정을 고쳐야 합니다.

1. EC2 콘솔 → **탄력적 IP** → **탄력적 IP 주소 할당**
2. 생성된 주소 선택 → **작업** → **탄력적 IP 주소 연결** → 대상 인스턴스 선택

> **과금 주의**: Elastic IP는 **실행 중인 인스턴스에 연결되어 있는 동안에만 무료**입니다.
> 인스턴스를 **중지(Stop)** 한 상태에서는 시간당 소액이 과금되며(월 $3~4 수준),
> 인스턴스를 **종료(Terminate)** 하는 경우에는 EIP도 **함께 해제**해야 과금이 멈춥니다.
> 데모까지 며칠간 유지하는 비용으로는 감수할 만하지만, 데모 종료 후에는 해제하십시오.

---

## 4. 🚨 Windows Defender 방화벽 — 가장 흔한 함정

> ### 이 단계를 건너뛰면 접속이 되지 않습니다. 그리고 원인을 보안 그룹으로 오인하게 됩니다.
>
> AWS 보안 그룹을 열어도 **Windows 자체 방화벽이 별도로 차단**합니다.
> 두 관문을 모두 통과해야 패킷이 서버에 도달합니다.
> 보안 그룹만 확인하고 "분명히 열었는데 왜 안 되지"로 시간을 쓰는 것이
> EC2 Windows 배포에서 가장 자주 발생하는 실수입니다.

RDP 접속 후 **PowerShell을 관리자 권한으로** 열고 다음 두 줄을 실행하십시오.

```powershell
New-NetFirewallRule -DisplayName "MMORPG GameServer 7777" -Direction Inbound -Protocol TCP -LocalPort 7777 -Action Allow
New-NetFirewallRule -DisplayName "MMORPG AuthAPI 5000"    -Direction Inbound -Protocol TCP -LocalPort 5000 -Action Allow
```

확인:

```powershell
Get-NetFirewallRule -DisplayName "MMORPG*" | Format-Table DisplayName, Enabled, Direction, Action
```

두 규칙이 `Enabled=True`, `Direction=Inbound`, `Action=Allow`로 보이면 됩니다.

---

## 5. Docker Desktop 설치 — 재부팅이 필요합니다

> **예상 소요 20~40분.** 이 가이드에서 가장 긴 단일 단계이며 **재부팅이 포함**됩니다.
> 시간을 확보한 상태에서 시작하십시오.

### 5-1. 컨테이너 기능 활성화 (재부팅 유발)

관리자 PowerShell에서:

```powershell
Install-WindowsFeature -Name Containers
Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux -NoRestart
Enable-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -NoRestart
Restart-Computer -Force
```

재부팅 후 RDP가 다시 열리기까지 **2~3분** 기다리십시오.

### 5-2. WSL2 커널 및 Docker Desktop

재부팅 후 관리자 PowerShell에서:

```powershell
wsl --update
wsl --set-default-version 2
```

이어서 Docker Desktop 설치 파일을 내려받아 실행합니다.

```powershell
Invoke-WebRequest -Uri "https://desktop.docker.com/win/main/amd64/Docker%20Desktop%20Installer.exe" `
                  -OutFile "$env:USERPROFILE\Downloads\DockerDesktopInstaller.exe"
Start-Process -Wait "$env:USERPROFILE\Downloads\DockerDesktopInstaller.exe" -ArgumentList "install --quiet --accept-license"
```

> **Internet Explorer 보안 강화 구성(IE ESC)** 때문에 다운로드가 막힐 수 있습니다.
> 그 경우 서버 관리자 → 로컬 서버 → **IE 보안 강화 구성**을 **끄기**로 변경하십시오.

설치 후 **한 번 더 재부팅**한 뒤 Docker Desktop을 실행하고, 트레이 아이콘이
"Docker Desktop is running"이 될 때까지 기다립니다.

```powershell
docker version    # Client와 Server 양쪽이 모두 표시되면 준비 완료
```

---

## 6. MySQL · Redis 컨테이너 기동

### 6-1. 컨테이너 실행

```powershell
docker run -d --name mmorpg-mysql `
  -e MYSQL_ROOT_PASSWORD=root `
  -e MYSQL_DATABASE=mmorpg_db `
  -p 127.0.0.1:3307:3306 `
  --restart unless-stopped `
  mysql:8.0

docker run -d --name mmorpg-redis `
  -p 127.0.0.1:6379:6379 `
  --restart unless-stopped `
  redis:7
```

> **`-p 127.0.0.1:3307:3306`의 앞부분이 중요합니다.** `-p 3307:3306`으로 쓰면
> 모든 인터페이스에 바인딩되어, Windows 방화벽 설정에 따라 외부에서 DB에 접근할 수 있게 됩니다.
> 루프백을 명시하면 **호스트 밖에서는 물리적으로 접근이 불가능**해집니다.

MySQL 초기화에 30~60초가 걸립니다. 다음이 성공하면 준비된 것입니다.

```powershell
docker exec mmorpg-mysql mysqladmin ping -uroot -proot
# mysqld is alive
```

### 6-2. 🚨 스키마 임포트 — 순서를 지키십시오

> **임포트 전에 기존 DB가 있다면 반드시 백업하십시오.**
> ```powershell
> docker exec mmorpg-mysql mysqldump -uroot -proot mmorpg_db > backup_before_import.sql
> ```

**임포트 순서:**

| 순서 | 파일 | 내용 |
|---|---|---|
| 1 | `Sql\schema.sql` | `Player` / `Mailbox` 테이블 (C++ 정본) |
| 2 | *(WebBackend 최초 기동)* | `Account` 테이블이 자동 생성됨 — §8-2에서 수행 |
| 3 | `Sql\seed_accounts.sql` | 테스트 계정 `test01`~`test05` |

```powershell
cd C:\MMORPG\DeployBundle
Get-Content .\Sql\schema.sql | docker exec -i mmorpg-mysql mysql -uroot -proot mmorpg_db
```

> ⚠️ **`mailbox.sql`은 임포트하지 마십시오.**
> 이 파일은 과거 `DROP TABLE IF EXISTS Player`로 시작해, `schema.sql` 뒤에 실행하면
> 방금 만든 `Player` 테이블을 **말없이 폐기하고 재생성**했습니다.
> 빈 DB에서는 증상이 없지만 **운영 중 재임포트 시 플레이어 데이터가 전부 사라집니다.**
> 2026-09-04에 `Player`/`Mailbox` DDL을 `schema.sql`로 통합했고, `mailbox.sql`은
> 경위만 남긴 빈 스크립트가 되었습니다. 실수로 실행해도 이제는 무해하지만,
> **오래된 사본을 쓰지 않도록 주의하십시오.**

확인:

```powershell
docker exec mmorpg-mysql mysql -uroot -proot -e "USE mmorpg_db; SHOW TABLES; DESCRIBE Player;"
```

`Player` 테이블에 `AccountId`, `Level`, `Gold`, `PosX/PosY/PosZ` 컬럼이 보여야 합니다.

---

## 7. 런타임 설치

### 7-1. VC++ 재배포 패키지 — 없으면 게임 서버가 즉시 종료됩니다

```powershell
Invoke-WebRequest -Uri "https://aka.ms/vs/17/release/vc_redist.x64.exe" `
                  -OutFile "$env:USERPROFILE\Downloads\vc_redist.x64.exe"
Start-Process -Wait "$env:USERPROFILE\Downloads\vc_redist.x64.exe" -ArgumentList "/install /quiet /norestart"
```

> 이것이 없으면 `GameServer.exe` 실행 시 **`VCRUNTIME140.dll을 찾을 수 없습니다`**
> 오류가 뜨고 프로세스가 즉시 종료됩니다. 서버 코드 문제로 오인하기 쉽습니다.

### 7-2. .NET 8 ASP.NET Core 런타임 — C# 백엔드 실행에 필요

```powershell
Invoke-WebRequest -Uri "https://dot.net/v1/dotnet-install.ps1" -OutFile "$env:TEMP\dotnet-install.ps1"
& "$env:TEMP\dotnet-install.ps1" -Channel 8.0 -Runtime aspnetcore -InstallDir "C:\Program Files\dotnet"

# PATH 등록 (현재 세션 + 영구)
$env:Path += ";C:\Program Files\dotnet"
[Environment]::SetEnvironmentVariable("Path", $env:Path, [EnvironmentVariableTarget]::Machine)

dotnet --list-runtimes   # Microsoft.AspNetCore.App 8.x 가 보이면 성공
```

> **`aspnetcore` 런타임이어야 합니다.** `-Runtime dotnet`(기본 런타임)만 설치하면
> 웹 호스팅 어셈블리가 없어 백엔드가 기동하지 않습니다.

---

## 8. 배포 번들 전송 및 기동

### 8-1. 번들 만들기 (로컬 PC에서)

```powershell
# 게임 서버 번들
cd D:\Mydev\AI_MMORPG_Project\Server
.\PackServer_Windows.bat        # → Server\DeployBundle\

# C# 백엔드 번들
cd D:\Mydev\AI_MMORPG_Project\WebBackend
dotnet publish -c Release -o .\publish
```

두 폴더를 RDP 드래그 앤 드롭(또는 S3 경유)으로 EC2의 `C:\MMORPG\` 아래에 복사합니다.

```
C:\MMORPG\
├── DeployBundle\        (GameServer.exe, DLL, Config.json, Sql\, DummyClient\)
└── WebBackend\          (dotnet publish 산출물)
```

> RDP 드래그 앤 드롭이 느리면(수백 MB) 로컬에서 `.zip`으로 압축해 옮긴 뒤
> EC2에서 `Expand-Archive`로 푸는 편이 빠릅니다.

### 8-2. 설정 확인 — 두 파일이 **같은 곳**을 봐야 합니다

`C:\MMORPG\DeployBundle\Config.json`:

```json
{
  "Server":   { "BindAddress": "0.0.0.0", "Port": 7777, "MaxSession": 1000 },
  "Database": {
    "MySQL": { "Host": "127.0.0.1", "Port": 3307, "User": "root",
               "Password": "root", "Database": "mmorpg_db" },
    "Redis": "tcp://127.0.0.1:6379"
  }
}
```

`C:\MMORPG\WebBackend\appsettings.Production.json`:

```json
{
  "ConnectionStrings": {
    "DefaultConnection": "Server=127.0.0.1;Port=3307;Database=mmorpg_db;User Id=root;Password=root;",
    "RedisConnection": "127.0.0.1:6379,abortConnect=false"
  },
  "Server": { "Urls": "http://0.0.0.0:5000" },
  "Auth":   { "TicketTtlSeconds": 120 }
}
```

**대조표 — 이 세 쌍이 일치해야 합니다:**

| 항목 | Config.json (C++) | appsettings.Production.json (C#) |
|---|---|---|
| MySQL 호스트·포트 | `127.0.0.1` / `3307` | `Server=127.0.0.1;Port=3307` |
| MySQL DB 이름 | `mmorpg_db` | `Database=mmorpg_db` |
| Redis | `tcp://127.0.0.1:6379` | `127.0.0.1:6379` |

> **`BindAddress`가 `0.0.0.0`인지 반드시 확인하십시오.** `127.0.0.1`이면 서버는
> 정상 기동하고 로그도 멀쩡하지만 **외부에서 절대 접속되지 않습니다.**
> C# 쪽의 대응 항목은 `Server:Urls`의 `0.0.0.0`입니다. 둘 다 같은 이유로 필요합니다.

### 8-3. C# 백엔드 기동

```powershell
cd C:\MMORPG\WebBackend
$env:ASPNETCORE_ENVIRONMENT = "Production"
.\WebBackend.exe
```

기동 로그에 다음이 보여야 합니다.

```
Now listening on: http://0.0.0.0:5000
EF Migration applied (Account only; Player is owned by schema.sql).
```

> **`EF Migration failed`가 보이면** MySQL이 아직 뜨지 않았거나 연결 문자열이 틀린 것입니다.
> 백엔드는 이 실패로 죽지 않으므로(의도된 동작) 로그를 반드시 확인하십시오.

이제 **테스트 계정을 시드**합니다 (Account 테이블이 방금 생성되었으므로 이 시점이 맞습니다).

```powershell
Get-Content C:\MMORPG\DeployBundle\Sql\seed_accounts.sql | docker exec -i mmorpg-mysql mysql -uroot -proot mmorpg_db
docker exec mmorpg-mysql mysql -uroot -proot -e "USE mmorpg_db; SELECT AccountId, AccountName FROM Account;"
```

### 8-4. 게임 서버 기동

**새 PowerShell 창**에서:

```powershell
cd C:\MMORPG\DeployBundle
.\GameServer.exe
```

---

## 9. 기동 확인

EC2 안에서:

```powershell
# ① 게임 서버가 리슨 중인가 (0.0.0.0:7777 이어야 함. 127.0.0.1:7777 이면 BindAddress 오류)
netstat -an | findstr 7777

# ② 백엔드가 리슨 중인가
netstat -an | findstr 5000

# ③ 백엔드 헬스 체크 — DB/Redis 연결까지 함께 점검합니다
curl.exe http://127.0.0.1:5000/api/auth/health
```

③의 응답에 `"database":"ok"`와 `"redis":"ok (...)"`가 모두 보여야 합니다.
하나라도 `error`면 게임 로그인도 실패하므로 여기서 먼저 해결하십시오.

**로컬 PC에서** (외부 접속 확인 — 여기까지 통과해야 진짜입니다):

```powershell
# ④ 로그인 API 외부 호출
curl.exe -X POST http://<ElasticIP>:5000/api/auth/login `
  -H "Content-Type: application/json" `
  -d '{\"AccountName\":\"test01\",\"Password\":\"test01\"}'
# → {"success":true,"ticket":"...","accountId":1,"playerId":1}

# ⑤ 게임 서버 외부 접속 (더미 세션 5개)
cd D:\Mydev\AI_MMORPG_Project\Server\DeployBundle\DummyClient
.\DummyClient.exe -ip=<ElasticIP> -port=7777 -sessions=5
```

**UE 클라이언트 설정** — 에디터에서 `AMC1GameInstance`의 디테일 패널:

| 속성 | 값 |
|---|---|
| `ServerIP` | `<ElasticIP>` |
| `ServerPort` | `7777` |
| `AuthBaseUrl` | `http://<ElasticIP>:5000` |
| `AccountName` / `Password` | `test01` / `test01` |
| `bBypassAuthServer` | `false` (정상 경로) |

또는 패키징 빌드 실행 시:

```
AMC1.exe -ServerIP=<ElasticIP> -ServerPort=7777 -AuthBaseUrl=http://<ElasticIP>:5000 -AccountName=test02 -Password=test02
```

---

## 10. 💰 비용 방어 — 작업이 끝나면 반드시

### 10-1. 인스턴스 중지 (매 테스트·세션 종료 시)

**콘솔**: EC2 → 인스턴스 선택 → **인스턴스 상태** → **인스턴스 중지**

**중지(Stop)와 종료(Terminate)의 차이:**

| | 중지 (Stop) | 종료 (Terminate) |
|---|---|---|
| 컴퓨팅 요금 | **0원** | 0원 |
| EBS 스토리지 요금 | 계속 부과 (50GiB ≈ 월 $4) | 없음 |
| 디스크 내용 | **보존** | **영구 삭제** |
| 재사용 | 다시 시작하면 그대로 | 불가 — 처음부터 다시 |

**데모까지는 반드시 "중지"를 쓰십시오.** 종료하면 Docker·런타임·번들을
전부 다시 설치해야 합니다(2~4시간 재작업).

### 10-2. 데모 종료 후 정리

1. 인스턴스 **종료(Terminate)**
2. **Elastic IP 해제** — 인스턴스가 없으면 EIP가 계속 과금됩니다
3. EBS 볼륨이 남아 있지 않은지 확인 (일반적으로 인스턴스와 함께 삭제됩니다)

---

## 11. 트러블슈팅

### 11-1. "게임 서버에 접속이 안 됩니다"

증상이 모두 똑같이 보이므로 **위에서부터 순서대로** 판별하십시오.

| # | 원인 | 판별 방법 | 조치 |
|---|---|---|---|
| 1 | **서버 미기동** | EC2에서 `netstat -an \| findstr 7777` → 아무것도 안 나옴 | `GameServer.exe` 실행. DLL 오류면 §7-1 |
| 2 | **BindAddress 오류** | 위 명령이 `127.0.0.1:7777`로 나옴 (`0.0.0.0`이 아님) | `Config.json`의 `BindAddress`를 `0.0.0.0`으로 |
| 3 | **Windows 방화벽** | EC2 안에서는 접속되나 밖에서는 안 됨 | §4의 규칙 2개 추가 |
| 4 | **보안 그룹** | 방화벽 규칙이 있는데도 밖에서 안 됨 | 인바운드 7777 `0.0.0.0/0` 확인 |
| 5 | **IP 오타** | UE 로그의 `Try Connecting to ...` 주소가 다름 | Elastic IP 재확인 |

> 클라이언트 로그에 `Failed to connect to <IP>:<Port>`가 남도록 되어 있으므로,
> **어디로 접속하려 했는지**를 먼저 확인하는 것이 가장 빠릅니다.

### 11-2. "로그인 API가 응답하지 않습니다"

| # | 원인 | 판별 | 조치 |
|---|---|---|---|
| 1 | .NET 런타임 없음 | `WebBackend.exe` 실행 시 프레임워크 오류 | §7-2, **aspnetcore** 런타임인지 확인 |
| 2 | 루프백에만 바인딩 | `netstat`에 `127.0.0.1:5000` | `appsettings.Production.json`의 `Server:Urls`를 `http://0.0.0.0:5000`으로 |
| 3 | **HTTP 307 응답** | curl이 307 Redirect 반환 | HTTPS 리다이렉트가 켜진 것. `ASPNETCORE_ENVIRONMENT=Production`인지 확인 (Development면 리다이렉트가 활성화됨) |
| 4 | 방화벽/보안 그룹 | 로컬에서만 됨 | §4, §2의 5000 규칙 |
| 5 | DB/Redis 연결 실패 | `/api/auth/health`가 `error` 반환 | 컨테이너 기동 및 §8-2 대조표 확인 |

### 11-3. "로그인은 되는데 게임 접속 후 바로 끊깁니다"

이 조합은 **티켓 전달 경로**의 문제입니다.

| # | 원인 | 판별 | 조치 |
|---|---|---|---|
| 1 | **서로 다른 Redis** | 백엔드와 게임 서버의 Redis 설정 비교 | §8-2 대조표 |
| 2 | **티켓 만료** | 로그인 후 접속까지 120초 초과 | `Auth:TicketTtlSeconds` 조정 |
| 3 | 티켓 재사용 | 같은 티켓으로 두 번 접속 시도 | 정상 동작(1회용). 다시 로그인 |
| 4 | 서버 콘솔에 `Malformed Ticket Payload` | Redis 값이 숫자가 아님 | 백엔드가 구버전(AccountId 저장). 최신 번들로 교체 |

**서버 콘솔에서 확인할 정상 로그:**

```
[ServerPacketHandler] C_LOGIN Received! Ticket: 3f2a...
[ServerPacketHandler] Login Success! Ticket: 3f2a... -> PlayerId 1
[GameSession] Player Loaded from DB. PlayerId=1 Name=test01 Gold=100 Pos=(0, 0, 0)
[Server] S_ENTER_GAME Sent! PlayerId: 1
```

### 11-4. "재접속했는데 이전 위치가 아닙니다"

| # | 확인 | 방법 |
|---|---|---|
| 1 | DB에 실제로 저장되는가 | `docker exec mmorpg-mysql mysql -uroot -proot -e "USE mmorpg_db; SELECT PlayerId, Name, Gold, PosX, PosY, PosZ FROM Player;"` |
| 2 | 저장 시점이 왔는가 | 자동 저장은 **60초 주기**입니다. 이동 직후 바로 끄면 저장 전일 수 있습니다. 정상 종료(로그아웃) 시에도 저장됩니다 |
| 3 | 같은 계정으로 접속했는가 | 서버 콘솔의 `PlayerId`가 두 번 다 같은 값인지 확인 |
| 4 | 클라이언트가 좌표를 적용했는가 | 화면에 자홍색 `★ [Restore] Loaded position from DB` 라인이 뜨는지 확인 |

> 4번이 뜨지 않으면 클라이언트 측 좌표 복원(F10 수정)이 빌드에 반영되지 않은 것입니다.

---

## 12. 체크리스트 (인쇄용)

```
[ ] 1. 인스턴스 생성 (Windows Server 2022 / t3.medium / gp3 50GiB / 키페어 보관)
[ ] 2. 보안 그룹: 7777 개방, 5000 개방, RDP 내 IP만, MySQL/Redis 규칙 없음
[ ] 3. Elastic IP 할당 및 연결
[ ] 4. ⚠️ Windows 방화벽 규칙 2개 추가 (7777, 5000)
[ ] 5. 컨테이너 기능 활성화 → 재부팅 → WSL2 → Docker Desktop → 재부팅
[ ] 6. MySQL(3307)·Redis(6379) 컨테이너 기동 (루프백 바인딩 확인)
[ ] 7. schema.sql 임포트  (⚠️ mailbox.sql은 임포트하지 않음)
[ ] 8. VC++ 재배포 패키지 설치
[ ] 9. .NET 8 aspnetcore 런타임 설치
[ ] 10. 번들 2개 전송 (DeployBundle, WebBackend publish)
[ ] 11. Config.json / appsettings.Production.json 대조표 확인
[ ] 12. WebBackend 기동 → Migration 로그 확인 → seed_accounts.sql 임포트
[ ] 13. GameServer 기동
[ ] 14. netstat 7777 / 5000 이 0.0.0.0 인지 확인
[ ] 15. /api/auth/health 가 database·redis 모두 ok
[ ] 16. 로컬 PC에서 로그인 API 외부 호출 성공
[ ] 17. 로컬 PC에서 DummyClient 5세션 접속 성공
[ ] 18. UE 클라이언트 설정 후 접속 → 이동 → 재접속 왕복 확인
[ ] 19. 💰 작업 종료 시 인스턴스 중지(Stop)
```
