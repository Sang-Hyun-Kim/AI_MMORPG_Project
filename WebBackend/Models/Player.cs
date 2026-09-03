using System.ComponentModel.DataAnnotations;
using System.ComponentModel.DataAnnotations.Schema;

namespace WebBackend.Models
{
    /*
     * Player
     * ─────────────────────────────────────────────────────────────────────────
     * [소유권 — 2026-09-04 확정]
     *   이 테이블의 DDL 정본은 `Server/schema.sql`이며 C++ GameServer가 소유합니다.
     *   C# WebBackend는 **소비자**입니다. 로그인 시 계정의 캐릭터를 찾고, 없으면
     *   만들기 위해서만 접근합니다.
     *
     *   ApplicationDbContext에서 이 엔티티를 ExcludeFromMigrations() 처리했으므로
     *   EF Core Migration은 이 테이블을 생성/변경/삭제하지 않습니다.
     *   ⚠️ 그 설정을 제거하면 마이그레이션이 C++ 정본 테이블을 덮어써
     *      게임 데이터(Level/Exp/Hp/좌표/골드)가 통째로 사라집니다.
     *
     * [과거 문제 — 결함 F2]
     *   이전 모델은 PlayerId / PlayerName / AccountId 3개 속성만 가지고 있어
     *   C++ 스키마(Name, Level, Exp, Hp, ClassId, Gold, PosX/Y/Z, Yaw)와
     *   완전히 다른 테이블을 정의했습니다. 같은 이름의 테이블이 두 벌 존재해
     *   어느 쪽이 정본인지 모호했고, 마이그레이션이 게임 테이블을 덮어쓸 위험이
     *   있었습니다. 아래 매핑으로 C++ 정본에 정렬했습니다.
     *
     * [컬럼명 주의]
     *   C++ 스키마의 컬럼은 `Name`입니다. C# 속성명은 가독성을 위해 PlayerName을
     *   유지하되 [Column("Name")]으로 매핑합니다. 이 특성을 지우면 EF가
     *   존재하지 않는 `PlayerName` 컬럼을 조회해 런타임에 실패합니다.
     */
    [Table("Player")]
    public class Player
    {
        [Key]
        [DatabaseGenerated(DatabaseGeneratedOption.Identity)]
        public long PlayerId { get; set; }

        // 계정↔캐릭터 매핑 컬럼. C++ GameServer는 이 컬럼을 읽지 않습니다.
        // (게임 서버는 PlayerId만 다루며 계정 도메인을 모릅니다 — 의도적 분리)
        public long AccountId { get; set; }

        [Required]
        [Column("Name")]
        [StringLength(50)]
        public string PlayerName { get; set; } = string.Empty;

        // ── 아래는 게임 런타임(C++)이 소유·갱신하는 상태입니다. ──
        // C#은 신규 캐릭터를 만들 때 초기값을 넣는 것 외에는 건드리지 않습니다.
        public int Level { get; set; } = 1;
        public int Exp { get; set; } = 0;
        public int Hp { get; set; } = 100;
        public int ClassId { get; set; } = 0;
        public int Gold { get; set; } = 0;
        public float PosX { get; set; } = 0f;
        public float PosY { get; set; } = 0f;
        public float PosZ { get; set; } = 0f;
        public float Yaw { get; set; } = 0f;

        [ForeignKey("AccountId")]
        public Account? Account { get; set; }
    }
}
