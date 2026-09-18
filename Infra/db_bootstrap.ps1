param(
    [string]$Container = "mysql_AI_MMORPG",
    [string]$EnvFile   = (Join-Path $PSScriptRoot ".env"),
    [string]$SchemaSql = (Join-Path $PSScriptRoot "..\Server\schema.sql")
)

# =====================================================================================
#  db_bootstrap.ps1 — 이미 초기화된 MySQL 볼륨에 정본 스키마를 멱등 적용 (TD-04 K4 · DB6)
#
#  docker-compose.yml 의 initdb 마운트는 **데이터 디렉터리가 빈 경우에만** 실행됩니다.
#  기존 볼륨(개발 PC · 운영 중인 EC2)에는 이 스크립트로 적용하십시오.
#
#  ⚠️ Server/schema.sql 이 유일한 정본입니다. 이 스크립트에 DDL 을 복사해 넣지 마십시오.
#  ⚠️ 비밀번호는 MYSQL_PWD 환경변수로만 전달합니다(명령줄 인자 금지) — 출력·로그에 남기지 마십시오.
#  ⚠️ 운영 DB 에 적용하기 전에는 mysqldump 로 백업하십시오(schema.sql 머리 주석).
#
#  종료 코드: 0 성공 · 1 사전 조건 실패 · 2 스키마 적용 실패 · 3 적용 후 검증 실패
# =====================================================================================

$ErrorActionPreference = "Stop"

"[db_bootstrap] container=$Container at $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

# --- 사전 조건 ---------------------------------------------------------------------
if (-not (Test-Path $SchemaSql)) {
    "[db_bootstrap] FAILED — 스키마 파일 없음: $SchemaSql"
    exit 1
}
if (-not (Test-Path $EnvFile)) {
    "[db_bootstrap] FAILED — 환경 파일 없음: $EnvFile (Infra/.env.example 를 복사해 만드십시오)"
    exit 1
}

$envMap = @{}
foreach ($line in (Get-Content $EnvFile)) {
    if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$') {
        $envMap[$Matches[1]] = $Matches[2].Trim().Trim('"').Trim("'")
    }
}
foreach ($key in @("DB_PASSWORD", "DB_NAME")) {
    if ([string]::IsNullOrWhiteSpace($envMap[$key])) {
        "[db_bootstrap] FAILED — $EnvFile 에 $key 가 없습니다"
        exit 1
    }
}
$dbName = $envMap["DB_NAME"]

$state = (docker inspect -f '{{.State.Status}}' $Container 2>$null)
if ($LASTEXITCODE -ne 0 -or $state.Trim() -ne "running") {
    "[db_bootstrap] FAILED — 컨테이너가 running 이 아닙니다: $Container (state=$state)"
    exit 1
}
"[db_bootstrap] 사전 조건 OK — db=$dbName schema=$((Resolve-Path $SchemaSql).Path)"

# --- 적용 ---------------------------------------------------------------------------
# 파일을 그대로 stdin 으로 넘깁니다(PowerShell 이 다시 인코딩하지 않도록 Start-Process 리디렉션).
$env:MYSQL_PWD = $envMap["DB_PASSWORD"]
$outFile = [IO.Path]::GetTempFileName()
$errFile = [IO.Path]::GetTempFileName()
try {
    $args = @("exec", "-i", "-e", "MYSQL_PWD", $Container,
              "mysql", "-uroot", "--default-character-set=utf8mb4", "--database=$dbName")
    $p = Start-Process -FilePath "docker" -ArgumentList $args -NoNewWindow -Wait -PassThru `
                       -RedirectStandardInput (Resolve-Path $SchemaSql).Path `
                       -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    $stdout = (Get-Content $outFile -Raw)
    $stderr = (Get-Content $errFile -Raw)
    if ($p.ExitCode -ne 0) {
        "[db_bootstrap] FAILED — 스키마 적용 실패 (exit $($p.ExitCode))"
        if ($stderr) { "[db_bootstrap]   stderr: $($stderr.Trim())" }
        exit 2
    }
    if ($stderr -and $stderr.Trim()) { "[db_bootstrap] (mysql stderr) $($stderr.Trim())" }
    if ($stdout -and $stdout.Trim()) { "[db_bootstrap] (mysql stdout) $($stdout.Trim())" }
    "[db_bootstrap] 스키마 적용 완료 (exit 0)"
}
finally {
    Remove-Item $outFile, $errFile -ErrorAction SilentlyContinue
    $env:MYSQL_PWD = $null
}

# --- 검증: 있어야 할 테이블이 있는가 (로그의 부재를 근거로 삼지 않는다) ----------------
$env:MYSQL_PWD = $envMap["DB_PASSWORD"]
try {
    $query = "SELECT TABLE_NAME FROM information_schema.TABLES WHERE TABLE_SCHEMA='$dbName' AND TABLE_NAME IN ('Player','Mailbox') ORDER BY TABLE_NAME"
    $found = (docker exec -i -e MYSQL_PWD $Container mysql -uroot -N -B -e $query) 2>$null
}
finally {
    $env:MYSQL_PWD = $null
}
$foundList = @($found | Where-Object { $_ -and $_.Trim() } | ForEach-Object { $_.Trim() })

$missing = @("Mailbox", "Player") | Where-Object { $foundList -notcontains $_ }
if ($missing.Count -gt 0) {
    "[db_bootstrap] FAILED — 적용 후에도 없는 테이블: $($missing -join ', ') (확인된 것: $($foundList -join ', '))"
    exit 3
}

"[db_bootstrap] PASS — Player · Mailbox 확인 (db=$dbName)"
exit 0
