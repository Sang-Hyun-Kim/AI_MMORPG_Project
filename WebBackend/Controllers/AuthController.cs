using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using StackExchange.Redis;
using WebBackend.Data;
using WebBackend.Models;

namespace WebBackend.Controllers
{
    /*
     * AuthController
     * ─────────────────────────────────────────────────────────────────────────
     * 풀스택 로그인 파이프라인에서 이 컨트롤러가 맡는 구간 (2026-09-04 확정):
     *
     *   ① UE 클라이언트가 AccountName/Password로 POST /api/auth/login
     *   ② 계정 검증
     *   ③ 그 계정의 캐릭터(Player 행)를 찾고, 없으면 생성 → PlayerId 확보
     *   ④ Redis에 Ticket:User:<ticket> → PlayerId 저장 (TTL)
     *   ⑤ 클라이언트에 ticket 반환
     *   → 이후 C++ GameServer가 C_LOGIN에서 그 티켓을 GET/DEL하고
     *     값(PlayerId)으로 캐릭터를 DB에서 로드합니다.
     *
     * [설계 의도] 게임 서버는 계정 도메인을 모릅니다. 계정↔캐릭터 매핑의 소유자는
     *   이 컨트롤러이며, 게임 서버에는 PlayerId만 건네줍니다. 덕분에 C++ 쪽 조회는
     *   언제나 `WHERE PlayerId = ?` 하나로 유지됩니다.
     *   ⚠️ 향후 캐릭터 선택 UI가 생기면 "계정의 여러 캐릭터 중 무엇을 고를지"를
     *      클라이언트가 결정해야 하므로, 이 전제(계정당 캐릭터 1개)를 재검토해야 합니다.
     */
    [Route("api/[controller]")]
    [ApiController]
    public class AuthController : ControllerBase
    {
        private readonly ApplicationDbContext _context;
        private readonly IConnectionMultiplexer _redis;
        private readonly IConfiguration _config;
        private readonly ILogger<AuthController> _logger;

        // 티켓 유효 시간. 원격(EC2) 환경에서는 로그인 응답 → TCP 접속 → C_LOGIN 까지
        // 왕복 지연이 있으므로 기본 120초로 둡니다. appsettings의 Auth:TicketTtlSeconds로 조정 가능.
        private const int DefaultTicketTtlSeconds = 120;

        public AuthController(
            ApplicationDbContext context,
            IConnectionMultiplexer redis,
            IConfiguration config,
            ILogger<AuthController> logger)
        {
            _context = context;
            _redis = redis;
            _config = config;
            _logger = logger;
        }

        [HttpPost("login")]
        public async Task<IActionResult> Login([FromBody] LoginRequest request)
        {
            if (string.IsNullOrEmpty(request.AccountName) || string.IsNullOrEmpty(request.Password))
            {
                return BadRequest("AccountName and Password are required.");
            }

            var account = await _context.Accounts
                .FirstOrDefaultAsync(a => a.AccountName == request.AccountName);

            // In a real application, passwords should be hashed and compared securely.
            // For portfolio purpose, we do a simple check.
            // ⚠️ 평문 비교입니다. Production 이행 전 해싱 도입 필수 (AI_HANDOFF.md 인계 항목).
            if (account == null || account.Password != request.Password)
            {
                _logger.LogWarning("Login failed for account '{AccountName}'.", request.AccountName);
                return Unauthorized("Invalid AccountName or Password.");
            }

            /*
             * [2026-09-04 추가] 계정의 캐릭터를 확보합니다.
             *
             * 변경 전: 이 단계가 아예 없었고, Redis 티켓에 AccountId를 넣었습니다.
             *   그런데 C++ 게임 서버는 그 값을 읽지도 않고 버렸으며(신원 미결속),
             *   대신 프로세스 카운터로 PlayerId를 발급했습니다. 그래서 재접속할 때마다
             *   다른 캐릭터가 되어 "저장 후 복원" 왕복이 성립하지 않았습니다. [F7]
             *
             * 변경 후: 계정의 캐릭터를 찾고 없으면 만들어 PlayerId를 확정합니다.
             *   이 PlayerId가 티켓에 실려 게임 서버까지 전달됩니다.
             */
            var player = await _context.Players
                .FirstOrDefaultAsync(p => p.AccountId == account.AccountId);

            if (player == null)
            {
                player = new Player
                {
                    AccountId = account.AccountId,
                    PlayerName = account.AccountName, // [Column("Name")]으로 C++ 정본에 매핑됨
                    Level = 1,
                    Exp = 0,
                    Hp = 100,
                    ClassId = 0,
                    Gold = 100,   // 초기 지급 골드 (C++ LoadPlayerTask의 신규 생성 기본값과 일치)
                    PosX = 0f,
                    PosY = 0f,
                    PosZ = 0f,
                    Yaw = 0f
                };

                _context.Players.Add(player);
                await _context.SaveChangesAsync(); // AUTO_INCREMENT로 PlayerId 확정

                _logger.LogInformation(
                    "New character created. AccountId={AccountId}, PlayerId={PlayerId}",
                    account.AccountId, player.PlayerId);
            }

            var ticket = Guid.NewGuid().ToString("N");
            var key = $"Ticket:User:{ticket}";
            var ttlSeconds = _config.GetValue("Auth:TicketTtlSeconds", DefaultTicketTtlSeconds);

            var db = _redis.GetDatabase();
            // 티켓의 값은 PlayerId입니다. (변경 전에는 AccountId였습니다.)
            // C++ Handle_C_LOGIN이 이 값을 숫자로 파싱해 세션에 결속하며,
            // 파싱에 실패하거나 0이면 접속을 거부합니다.
            await db.StringSetAsync(key, player.PlayerId.ToString(), TimeSpan.FromSeconds(ttlSeconds));

            _logger.LogInformation(
                "Login success. Account='{AccountName}' PlayerId={PlayerId} TicketTtl={Ttl}s",
                account.AccountName, player.PlayerId, ttlSeconds);

            return Ok(new LoginResponse
            {
                Success = true,
                Ticket = ticket,
                AccountId = account.AccountId,
                PlayerId = player.PlayerId
            });
        }

        /*
         * 헬스 체크 — EC2 배포 후 "C#이 살아 있는가"를 게임 접속 전에 확인하기 위한 용도입니다.
         * 프로비저닝 가이드의 기동 확인 절차에서 curl로 호출합니다.
         * DB/Redis 연결까지 함께 점검하므로, 여기서 실패하면 로그인도 실패합니다.
         */
        [HttpGet("health")]
        public async Task<IActionResult> Health()
        {
            var result = new Dictionary<string, object>
            {
                ["service"] = "ok",
                ["utc"] = DateTime.UtcNow.ToString("O")
            };

            try
            {
                result["accountCount"] = await _context.Accounts.CountAsync();
                result["database"] = "ok";
            }
            catch (Exception ex)
            {
                result["database"] = $"error: {ex.Message}";
            }

            try
            {
                var pong = await _redis.GetDatabase().PingAsync();
                result["redis"] = $"ok ({pong.TotalMilliseconds:F1}ms)";
            }
            catch (Exception ex)
            {
                result["redis"] = $"error: {ex.Message}";
            }

            return Ok(result);
        }
    }

    public class LoginRequest
    {
        public string AccountName { get; set; } = string.Empty;
        public string Password { get; set; } = string.Empty;
    }

    public class LoginResponse
    {
        public bool Success { get; set; }
        public string Ticket { get; set; } = string.Empty;
        public int AccountId { get; set; }

        // [2026-09-04 추가] 클라이언트가 자신의 캐릭터 ID를 미리 알 수 있게 함께 내려줍니다.
        // 게임 서버도 S_ENTER_GAME으로 같은 값을 보내므로 클라이언트는 양쪽을 대조해
        // 신원이 어긋나면 즉시 알아챌 수 있습니다(디버깅 편의).
        public long PlayerId { get; set; }
    }
}
