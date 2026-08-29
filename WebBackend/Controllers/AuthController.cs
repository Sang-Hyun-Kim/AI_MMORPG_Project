using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using StackExchange.Redis;
using WebBackend.Data;

namespace WebBackend.Controllers
{
    [Route("api/[controller]")]
    [ApiController]
    public class AuthController : ControllerBase
    {
        private readonly ApplicationDbContext _context;
        private readonly IConnectionMultiplexer _redis;

        public AuthController(ApplicationDbContext context, IConnectionMultiplexer redis)
        {
            _context = context;
            _redis = redis;
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
            if (account == null || account.Password != request.Password)
            {
                return Unauthorized("Invalid AccountName or Password.");
            }

            var ticket = Guid.NewGuid().ToString("N");
            var key = $"Ticket:User:{ticket}";

            var db = _redis.GetDatabase();
            // Set ticket in Redis with an expiration of 30 seconds
            await db.StringSetAsync(key, account.AccountId.ToString(), TimeSpan.FromSeconds(30));

            return Ok(new LoginResponse
            {
                Success = true,
                Ticket = ticket,
                AccountId = account.AccountId
            });
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
    }
}
