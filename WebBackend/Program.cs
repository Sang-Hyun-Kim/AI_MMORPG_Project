using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Diagnostics;   // RelationalEventId — 아래 [C-7] 참조
using StackExchange.Redis;
using WebBackend.Data;

var builder = WebApplication.CreateBuilder(args);

// Add services to the container.
builder.Services.AddControllers();
builder.Services.AddOpenApi();

/*
 * [2026-09-04] 외부 바인딩 — 결함 C-4
 *
 * 문제: launchSettings.json의 applicationUrl이 http://localhost:5219 뿐이라
 *   Kestrel이 루프백에만 바인딩되었습니다. EC2에 배포해도 **외부에서 API에 닿지
 *   않습니다.** 이것은 직전 세션이 게임 서버에서 잡은 [B1](리슨 주소를
 *   127.0.0.1로 하드코딩)과 **정확히 같은 종류의 결함**입니다.
 *   더구나 launchSettings.json은 `dotnet run` 개발 실행에만 쓰이고
 *   게시본(dotnet publish 후 exe 실행)에는 적용되지 않으므로, 그 파일만 고쳐서는
 *   운영 환경에서 아무 효과가 없습니다.
 *
 * 조치: 설정(Server:Urls) → 환경변수(ASPNETCORE_URLS) → 기본값 순으로 해석하고,
 *   기본값을 0.0.0.0:5000으로 둡니다. 게임 서버 Config.json의
 *   Server.BindAddress = "0.0.0.0"과 같은 사상입니다.
 *
 * ⚠️ 이 호출을 지우면 EC2 외부 접속이 조용히 실패합니다. 보안 그룹이나 방화벽을
 *   의심하게 되어 원인 추적이 오래 걸리는 유형입니다.
 */
var urls = builder.Configuration["Server:Urls"];
if (string.IsNullOrWhiteSpace(urls))
    urls = Environment.GetEnvironmentVariable("ASPNETCORE_URLS");
if (string.IsNullOrWhiteSpace(urls))
    urls = "http://0.0.0.0:5000";
builder.WebHost.UseUrls(urls);

// Configure MySQL (Pomelo EntityFrameworkCore)
// 기본값은 C++ GameServer의 Config.json과 동일한 좌표(127.0.0.1:3307 / mmorpg_db)입니다. [F3]
// 두 서버가 서로 다른 DB를 보면 C#이 만든 캐릭터를 C++가 찾지 못합니다.
var connectionString = builder.Configuration.GetConnectionString("DefaultConnection")
    ?? "Server=127.0.0.1;Port=3307;Database=mmorpg_db;User Id=root;Password=root;";
/*
 * [2026-09-04] PendingModelChangesWarning 억제 — 결함 C-7 (표시 문제)
 *
 * 증상: 기동할 때마다 아래 줄이 `fail:` 레벨로 출력됩니다.
 *
 *     fail: Microsoft.EntityFrameworkCore.Migrations[20409]
 *           The model for context 'ApplicationDbContext' has pending changes.
 *           Add a new migration before updating the database.
 *
 *   `fail:`로 찍히지만 **기능에는 영향이 없습니다.** 바로 다음 줄에
 *   `No migrations were applied. The database is already up to date.`가 출력되고
 *   정상 기동하며, 로그인도 HTTP 200으로 성공합니다(2026-09-04 실행 검증).
 *   그럼에도 억제하는 이유는 **데모 중 이 줄을 실제 장애로 오인하기 쉽기 때문**입니다.
 *   실제로 사용자가 이 줄을 실패 로그로 보고했습니다.
 *
 * 원인: `Migrations/ApplicationDbContextModelSnapshot.cs`에 **구(舊) Player 모델**이
 *   남아 있습니다. (`PlayerId`가 `int`, 컬럼명이 `PlayerName`, 유니크 인덱스 존재,
 *   속성 3개, `ExcludeFromMigrations` 없음, FK 관계 선언) 현재 모델과 차이가 크므로
 *   EF가 "마이그레이션을 추가하라"고 경고합니다. 이것이 취약점 대장의 **V7**입니다.
 *
 * ⚠️ **이 억제는 증상만 가립니다. V7 자체는 그대로 남아 있습니다.**
 *   스냅샷이 구 모델을 기록하고 있으므로, `dotnet ef migrations add`를 실행하면
 *   "EF 소유 Player" → "제외된 Player" 차분이 **`DropTable("Player")`로 생성됩니다.**
 *   (2026-09-04 스냅샷 실물 확인. 가설이 아니라 확인된 사실입니다.)
 *   → 새 마이그레이션을 추가할 때는 **반드시 생성된 `Up()`을 눈으로 검토하고
 *      `Player` 조작을 제거**하십시오. 놓치면 게임 데이터가 전소됩니다.
 *
 * 근본 해결(데모 후): 빈 마이그레이션을 하나 추가해 스냅샷을 현재 모델에 맞춘 뒤
 *   이 `ConfigureWarnings` 호출을 제거합니다. 그러면 경고가 다시 **유효한 신호**가
 *   되어, 앞으로 모델을 고치고 마이그레이션을 잊었을 때 알려줍니다.
 *   V15(AccountId 타입 정렬)와 함께 처리하는 것이 효율적입니다.
 */
builder.Services.AddDbContext<ApplicationDbContext>(options =>
    options
        .UseMySql(connectionString, new MySqlServerVersion(new Version(8, 0, 32)))
        .ConfigureWarnings(w => w.Ignore(RelationalEventId.PendingModelChangesWarning)));

// Configure Redis
// 게임 서버(Config.json의 Database.Redis)와 반드시 같은 인스턴스를 가리켜야 합니다.
// 티켓은 이 Redis를 통해 C# → C++로 전달됩니다.
var redisConnectionString = builder.Configuration.GetConnectionString("RedisConnection")
    ?? "127.0.0.1:6379,abortConnect=false";
builder.Services.AddSingleton<IConnectionMultiplexer>(
    ConnectionMultiplexer.Connect(redisConnectionString));

var app = builder.Build();

/*
 * [2026-09-04] 시작 시 EF Migration 자동 적용
 *
 * 이유: EC2에 dotnet-ef CLI 도구를 설치하지 않아도 Account 테이블이 만들어지도록
 *   하기 위함입니다. 프로비저닝 단계가 하나 줄어듭니다.
 *   Player 테이블은 ApplicationDbContext에서 ExcludeFromMigrations 처리되어 있어
 *   이 호출이 건드리지 않습니다. (Player의 정본은 Server/schema.sql입니다.)
 *
 * 실패해도 프로세스를 죽이지 않습니다. DB가 아직 안 떠 있는 기동 순서 문제로
 * 서비스 전체가 내려가면 원인 파악이 더 어려워지기 때문입니다. 대신 로그에 남깁니다.
 */
using (var scope = app.Services.CreateScope())
{
    var logger = scope.ServiceProvider.GetRequiredService<ILogger<Program>>();
    try
    {
        var db = scope.ServiceProvider.GetRequiredService<ApplicationDbContext>();
        db.Database.Migrate();
        logger.LogInformation("EF Migration applied (Account only; Player is owned by schema.sql).");
    }
    catch (Exception ex)
    {
        logger.LogError(ex, "EF Migration failed at startup. "
            + "MySQL이 기동했는지, 연결 문자열이 게임 서버 Config.json과 같은지 확인하십시오.");
    }
}

// Configure the HTTP request pipeline.
if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
}

/*
 * [2026-09-04] HTTPS 리다이렉트를 개발 환경에서만 적용 — 결함 C-5
 *
 * 문제: UseHttpsRedirection()이 무조건 켜져 있었습니다. EC2에는 TLS 인증서가 없고
 *   HTTP 엔드포인트만 여는데, 이 미들웨어가 UE 클라이언트의 HTTP 요청에
 *   307 Temporary Redirect를 돌려주고 리다이렉트 대상인 HTTPS는 열려 있지 않아
 *   **로그인 요청이 실패합니다.** 원인이 클라이언트 코드처럼 보여 추적이 어렵습니다.
 *
 * 조치: 개발 환경(로컬 dotnet run)에서는 기존대로 유지하고, 그 외(운영/EC2)에서는
 *   적용하지 않습니다. 정식 도메인과 인증서를 갖추면 다시 켜야 하며,
 *   AI_HANDOFF.md에 인계 항목으로 기록되어 있습니다.
 */
if (app.Environment.IsDevelopment())
{
    app.UseHttpsRedirection();
}

app.UseAuthorization();
app.MapControllers();

app.Run();
