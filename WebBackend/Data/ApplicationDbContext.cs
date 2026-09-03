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
