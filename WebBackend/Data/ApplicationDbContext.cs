using Microsoft.EntityFrameworkCore;
using WebBackend.Models;

namespace WebBackend.Data
{
    public class ApplicationDbContext : DbContext
    {
        public ApplicationDbContext(DbContextOptions<ApplicationDbContext> options)
            : base(options)
        {
        }

        public DbSet<Account> Accounts { get; set; }
        public DbSet<Player> Players { get; set; }

        protected override void OnModelCreating(ModelBuilder modelBuilder)
        {
            /*
             * [DDL 소유권 분리 — 2026-09-04 확정]
             *
             *   Account : EF Core Migration이 소유합니다. (C# 인증 도메인의 테이블)
             *   Player  : Server/schema.sql이 소유합니다. (C++ 게임 런타임의 테이블)
             *
             * 왜 나누는가:
             *   Player는 게임 서버가 매 틱 읽고 쓰는 테이블이므로 DDL의 주인은
             *   게임 서버여야 하고, C#은 소비자입니다. 반대로 Account는 C# 인증
             *   로직만 사용하므로 EF가 관리하는 편이 자연스럽습니다.
             *
             * ⚠️ ExcludeFromMigrations()를 제거하지 마십시오.
             *   제거하면 다음 마이그레이션이 C++ 정본 Player 테이블을 자신의 모델대로
             *   재정의하려 들고, Level/Exp/Hp/좌표/골드 컬럼과 그 안의 데이터가
             *   사라집니다. (2026-09-04 이전 Init 마이그레이션이 실제로 그런 상태였고,
             *   그대로 EC2에 적용했다면 게임 테이블이 파손되었을 것입니다. — 결함 F2/R4)
             */
            modelBuilder.Entity<Player>()
                .ToTable("Player", t => t.ExcludeFromMigrations());

            /*
             * [2026-09-04 추가] Account ↔ Player 관계를 매핑에서 제외 — 결함 C-6
             *
             * ── 증상 ──────────────────────────────────────────────────────────
             *   · 기동 시  : fail: Program[0] "EF Migration failed at startup"
             *   · 로그인 시: POST /api/auth/login 이 HTTP 500
             *   두 증상은 원인이 같습니다.
             *
             *     System.InvalidOperationException:
             *       The relationship from 'Player.Account' to 'Account.Players'
             *       with foreign key properties {'AccountId' : long} cannot target
             *       the primary key {'AccountId' : int} because it is not compatible.
             *
             * ── 원인 ──────────────────────────────────────────────────────────
             *   Player.AccountId 는 long 입니다. schema.sql의 `AccountId bigint(20)`
             *   (C++ 정본)에 맞춘 값이며 이것은 옳습니다. [C-3]
             *   반면 Account.AccountId 는 int 입니다. Account 테이블은 EF가 소유하고
             *   Init 마이그레이션이 `int` 로 만들었습니다.
             *
             *   내비게이션 프로퍼티(Player.Account / Account.Players)가 이 둘을 FK 관계로
             *   선언하는데, EF는 FK(long)와 주 키(int)의 타입이 다르면 **모델을 조립하는
             *   단계에서** 예외를 던집니다. 모델 조립은 DbContext를 처음 사용할 때
             *   일어나므로, Migrate() 도 로그인 쿼리도 같은 지점에서 죽습니다.
             *
             *   ⚠️ 이것은 컴파일 에러가 아닙니다. `dotnet build`는 통과합니다.
             *      2026-09-04 세션이 빌드만 확인하고 실제 기동을 하지 않아 놓쳤습니다.
             *
             * ── 왜 이 방법으로 고치는가 ────────────────────────────────────────
             *   이 관계는 **선언만 되어 있고 실제로 쓰이지 않습니다.**
             *   코드 전체에 .Include() 가 없고, AuthController는 관계를 타지 않고
             *   `p.AccountId == account.AccountId` 값 비교로 캐릭터를 찾습니다.
             *   지연 로딩도 켜져 있지 않습니다. 즉 관계를 매핑에서 빼도 동작이 바뀌지
             *   않으며, DDL·데이터·마이그레이션을 전혀 건드리지 않습니다.
             *
             *   프로퍼티 자체는 **삭제하지 않았습니다.** 타입 정렬 후 관계를 되살릴
             *   여지를 남기기 위함이며, 여기서는 매핑에서만 제외합니다.
             *
             * ── 검토했으나 채택하지 않은 대안 ──────────────────────────────────
             *   (A) Player.AccountId 를 int 로 낮춘다
             *       → schema.sql이 bigint 이므로 C# 모델이 정본을 잘못 기술하게 됩니다. [C-3 위반]
             *   (B) Account.AccountId 를 long 으로 올리고 ALTER TABLE 한다
             *       → **이쪽이 근본 해결이며 데모 후 처리 대상입니다(V15).**
             *          지금 하지 않는 이유: 새 마이그레이션이 필요한데,
             *          Migrations 스냅샷에 구(舊) Player 모델이 남아 있어(V7)
             *          `dotnet ef migrations add` 가 DropTable("Player") 를 생성할
             *          위험이 있습니다. 데모 전날에 감수할 위험이 아닙니다.
             *
             * ⚠️ 관계를 되살리려면 **반드시 두 AccountId 의 타입을 먼저 맞추십시오.**
             *    타입이 어긋난 채로 이 두 줄을 지우면 서버 전체가 다시 500이 됩니다.
             */
            modelBuilder.Entity<Player>().Ignore(p => p.Account);
            modelBuilder.Entity<Account>().Ignore(a => a.Players);

            modelBuilder.Entity<Account>()
                .HasIndex(a => a.AccountName)
                .IsUnique();

            /*
             * [제거됨] Player.PlayerName 유니크 인덱스
             *
             *   과거: modelBuilder.Entity<Player>().HasIndex(p => p.PlayerName).IsUnique();
             *
             *   이 인덱스는 C++ 정본 스키마(schema.sql)에 존재하지 않습니다.
             *   EF가 Player DDL을 더 이상 소유하지 않으므로 여기서 인덱스를 선언하면
             *   실제 DB와 모델이 어긋납니다. 캐릭터명 유일성이 필요해지면
             *   schema.sql에 UNIQUE KEY를 추가하는 것이 옳은 위치입니다.
             *   (현재는 신규 캐릭터명을 계정명으로 만들고 있어 Account의 유니크
             *    제약이 사실상 같은 역할을 합니다.)
             */
        }
    }
}
