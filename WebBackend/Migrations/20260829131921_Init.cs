using Microsoft.EntityFrameworkCore.Metadata;
using Microsoft.EntityFrameworkCore.Migrations;

#nullable disable

namespace WebBackend.Migrations
{
    /// <inheritdoc />
    public partial class Init : Migration
    {
        /// <inheritdoc />
        protected override void Up(MigrationBuilder migrationBuilder)
        {
            migrationBuilder.AlterDatabase()
                .Annotation("MySql:CharSet", "utf8mb4");

            migrationBuilder.CreateTable(
                name: "Account",
                columns: table => new
                {
                    AccountId = table.Column<int>(type: "int", nullable: false)
                        .Annotation("MySql:ValueGenerationStrategy", MySqlValueGenerationStrategy.IdentityColumn),
                    AccountName = table.Column<string>(type: "varchar(50)", maxLength: 50, nullable: false)
                        .Annotation("MySql:CharSet", "utf8mb4"),
                    Password = table.Column<string>(type: "varchar(100)", maxLength: 100, nullable: false)
                        .Annotation("MySql:CharSet", "utf8mb4")
                },
                constraints: table =>
                {
                    table.PrimaryKey("PK_Account", x => x.AccountId);
                })
                .Annotation("MySql:CharSet", "utf8mb4");

            /*
             * [2026-09-04 수정] Player 테이블 생성 블록을 제거했습니다. — 결함 F2 / 리스크 R4
             *
             * 제거 전 이 마이그레이션은 다음을 수행했습니다.
             *     CreateTable("Player") { PlayerId, PlayerName, AccountId }
             *       + FK_Player_Account_AccountId (ON DELETE CASCADE)
             *       + IX_Player_AccountId, IX_Player_PlayerName(unique)
             *     Down(): DropTable("Player")
             *
             * 무엇이 문제였나:
             *   Player는 C++ GameServer가 소유하는 게임 런타임 테이블이며 정본은
             *   Server/schema.sql입니다. 정본에는 Name, Level, Exp, Hp, ClassId, Gold,
             *   PosX/PosY/PosZ, Yaw 컬럼이 있는데, 이 마이그레이션은 **3개 컬럼짜리
             *   전혀 다른 Player**를 만들려 했습니다. EC2에서 이 마이그레이션을 적용했다면
             *   게임 테이블과 충돌하거나(이미 존재 시 실패) 게임 상태 컬럼이 없는
             *   테이블이 생겨 서버가 조회에 실패했을 것입니다.
             *   Down()의 DropTable("Player")는 롤백 시 게임 데이터를 전부 삭제합니다.
             *
             * 조치:
             *   Player는 schema.sql이 만들고, EF는 ApplicationDbContext에서
             *   ExcludeFromMigrations()로 제외해 읽기만 합니다.
             *   이 마이그레이션은 이제 Account 테이블만 책임집니다.
             *
             * ⚠️ 주의: Migrations/*.Designer.cs 와 ApplicationDbContextModelSnapshot.cs
             *   에는 아직 구(舊) Player 모델이 기록되어 있습니다. 지금은 마이그레이션을
             *   추가하지 않으므로 무해하지만, 향후 `dotnet ef migrations add`를 실행하면
             *   스냅샷과 현재 모델의 차이 때문에 예상치 못한 DDL이 생성될 수 있습니다.
             *   새 마이그레이션을 추가하기 전에 반드시 생성된 Up()을 눈으로 검토하고,
             *   Player에 대한 조작이 들어 있으면 제거하십시오.
             */

            migrationBuilder.CreateIndex(
                name: "IX_Account_AccountName",
                table: "Account",
                column: "AccountName",
                unique: true);
        }

        /// <inheritdoc />
        protected override void Down(MigrationBuilder migrationBuilder)
        {
            // Player는 이 마이그레이션이 만들지 않으므로 되돌릴 때도 건드리지 않습니다.
            // (과거의 DropTable("Player")는 롤백 시 게임 데이터를 전부 삭제했습니다.)
            migrationBuilder.DropTable(
                name: "Account");
        }
    }
}
