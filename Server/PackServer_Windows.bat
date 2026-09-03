@echo off
setlocal EnableDelayedExpansion
chcp 65001 > nul

REM ============================================================================
REM  PackServer_Windows.bat
REM  역할 : AWS EC2(Windows Server 2022)로 옮길 GameServer 배포 번들을 생성합니다.
REM  작성 : 2026-09-03 (Phase 4 Item 2)
REM
REM  사용법:
REM     PackServer_Windows.bat            번들 갱신(기존 파일 덮어쓰기)
REM     PackServer_Windows.bat clean      번들 폴더를 비우고 새로 생성
REM
REM  ※ 'clean' 인수를 주지 않으면 어떤 파일도 삭제하지 않고 덮어쓰기만 합니다.
REM    (의도치 않은 삭제를 막기 위한 안전장치)
REM ============================================================================

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build"
set "BUNDLE=%SCRIPT_DIR%DeployBundle"
set "SRC_GS=%BUILD_DIR%\GameServer\Release"
set "SRC_DC=%BUILD_DIR%\DummyClient\Release"

echo.
echo ============================================================
echo   GameServer 배포 번들 생성 (Release)
echo ============================================================
echo   빌드 폴더 : %BUILD_DIR%
echo   출력 폴더 : %BUNDLE%
echo.

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [ERROR] CMake 빌드 폴더를 찾을 수 없습니다: %BUILD_DIR%
    echo         먼저 CMake configure를 수행하세요.
    exit /b 1
)

REM ---------------------------------------------------------------- 1) 정리
if /I "%~1"=="clean" (
    if exist "%BUNDLE%" (
        echo [1/5] 기존 번들 폴더를 비웁니다...
        rmdir /s /q "%BUNDLE%"
    )
) else (
    echo [1/5] 정리 생략 ^(덮어쓰기 모드^). 완전히 새로 만들려면: PackServer_Windows.bat clean
)

REM ---------------------------------------------------------------- 2) 빌드
echo [2/5] Release 구성으로 빌드합니다...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo.
    echo [ERROR] Release 빌드 실패. 번들 생성을 중단합니다.
    exit /b 1
)

if not exist "%SRC_GS%\GameServer.exe" (
    echo [ERROR] GameServer.exe 를 찾을 수 없습니다: %SRC_GS%
    exit /b 1
)

REM ---------------------------------------------------------------- 3) 복사
echo [3/5] 서버 바이너리와 런타임 DLL을 복사합니다...
if not exist "%BUNDLE%"            mkdir "%BUNDLE%"
if not exist "%BUNDLE%\DummyClient" mkdir "%BUNDLE%\DummyClient"
if not exist "%BUNDLE%\Sql"         mkdir "%BUNDLE%\Sql"

copy /Y "%SRC_GS%\GameServer.exe" "%BUNDLE%\" > nul
copy /Y "%SRC_GS%\Config.json"    "%BUNDLE%\" > nul
copy /Y "%SRC_GS%\*.dll"          "%BUNDLE%\" > nul

REM DummyClient (데모 Step 3: 외부망 다중 세션 접속에 사용)
if exist "%SRC_DC%\DummyClient.exe" (
    copy /Y "%SRC_DC%\DummyClient.exe" "%BUNDLE%\DummyClient\" > nul
    copy /Y "%SRC_DC%\*.dll"           "%BUNDLE%\DummyClient\" > nul
) else (
    echo   [WARN] DummyClient.exe 없음 - 건너뜁니다.
)

REM DB 스키마
if exist "%SCRIPT_DIR%schema.sql"  copy /Y "%SCRIPT_DIR%schema.sql"  "%BUNDLE%\Sql\" > nul
if exist "%SCRIPT_DIR%seed_accounts.sql" copy /Y "%SCRIPT_DIR%seed_accounts.sql" "%BUNDLE%\Sql\" > nul
rem [2026-09-04] mailbox.sql 은 더 이상 번들에 넣지 않습니다.
rem   과거 이 파일은 DROP TABLE IF EXISTS Player 로 시작해, schema.sql 뒤에 임포트하면
rem   방금 만든 Player 테이블을 폐기하고 재생성했습니다(운영 중 재임포트 시 데이터 전소).
rem   Player/Mailbox DDL 은 schema.sql 로 통합되었습니다. [F4]

REM ---------------------------------------------------------------- 4) 안내문
echo [4/5] 배포 안내문을 생성합니다...
(
echo # GameServer 배포 번들
echo.
echo 생성 시각: %DATE% %TIME%
echo.
echo ## 배포 절차 ^(AWS EC2 Windows Server 2022^)
echo.
echo 1. 이 폴더 전체를 EC2로 복사합니다 ^(RDP 드래그 앤 드롭 또는 S3 경유^).
echo 2. **Microsoft Visual C++ 재배포 가능 패키지 ^(x64^)** 를 EC2에 설치합니다.
echo    https://aka.ms/vs/17/release/vc_redist.x64.exe
echo    ^-^> 이것이 없으면 GameServer.exe 가 VCRUNTIME140.dll 오류로 즉시 종료됩니다.
echo 3. MySQL^(3307^) 과 Redis^(6379^) 를 기동하고 Sql\schema.sql 을 임포트합니다.
echo    ^(WebBackend 최초 기동으로 Account 테이블이 생성된 뒤 Sql\seed_accounts.sql 을 임포트하십시오^)
echo 4. Config.json 을 환경에 맞게 수정합니다.
echo    - Server.BindAddress : 0.0.0.0 유지 ^(외부 접속 허용^)
echo    - Database.MySQL     : EC2 내 DB 접속 정보
echo 5. **Windows Defender 방화벽**에 7777 인바운드 규칙을 추가합니다.
echo    AWS 보안 그룹과는 별개입니다. 이 단계를 빠뜨리면 접속이 안 되는데
echo    원인을 보안 그룹으로 오인하기 쉽습니다.
echo    powershell: New-NetFirewallRule -DisplayName "MMORPG GameServer 7777" ^^
echo                -Direction Inbound -Protocol TCP -LocalPort 7777 -Action Allow
echo 6. GameServer.exe 를 실행합니다.
echo.
echo ## DummyClient 사용법 ^(외부망 다중 세션 테스트^)
echo.
echo     DummyClient.exe -ip=^<EC2 Elastic IP^> -port=7777 -sessions=5
echo.
echo ## UE5 클라이언트 접속
echo.
echo   - 에디터: UAMC1GameInstance 의 ServerIP / ServerPort 를 편집
echo   - 패키지: AMC1.exe -ServerIP=^<EC2 Elastic IP^> -ServerPort=7777
echo.
echo ## 비용 주의
echo.
echo   테스트 종료 시 EC2 인스턴스를 반드시 **중지^(Stop^)** 하세요.
) > "%BUNDLE%\README_DEPLOY.md"

REM ---------------------------------------------------------------- 5) 요약
echo [5/5] 완료.
echo.
echo ============================================================
echo   번들 생성 완료: %BUNDLE%
echo ============================================================
dir /b "%BUNDLE%"
echo.
echo   * VC++ 재배포 패키지는 번들에 포함되지 않습니다.
echo     EC2에 vc_redist.x64.exe 를 직접 설치하세요 ^(README_DEPLOY.md 참고^).
echo.

endlocal
exit /b 0
