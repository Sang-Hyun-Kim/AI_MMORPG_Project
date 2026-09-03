-- ============================================================================
--  AI_MMORPG_Project — 게임 DB 정본 스키마 (Single Source of Truth)
-- ============================================================================
--  [소유권 / 2026-09-04 확정]
--    이 파일이 `Player` / `Mailbox` 테이블 DDL의 **유일한 정본**입니다.
--    - C++ GameServer : 이 스키마를 런타임에 매 틱 읽고 씁니다. (소유자)
--    - C# WebBackend  : 이 스키마를 **소비만** 합니다.
--                       EF Core Migration이 `Player`를 생성/변경하지 않도록
--                       ApplicationDbContext에서 ExcludeFromMigrations() 처리했습니다.
--
--  [임포트 순서 — 반드시 지킬 것]
--    1) schema.sql        (이 파일)
--    2) seed_accounts.sql (테스트 계정)
--    ※ 과거 mailbox.sql이 `DROP TABLE IF EXISTS Player`로 시작해
--      schema.sql 뒤에 임포트하면 Player를 조용히 폐기했습니다. [F4]
--      2026-09-04에 mailbox.sql의 중복 Player DDL을 제거하고 이 파일로 통합했습니다.
--    ※ 운영 중인 DB에 재임포트하기 전에는 반드시 mysqldump로 백업하십시오.
-- ============================================================================

CREATE TABLE IF NOT EXISTS `Player` (
  `PlayerId`  bigint(20)  NOT NULL AUTO_INCREMENT,
  -- [F2/Q9] 계정↔캐릭터 매핑 컬럼. C# WebBackend가 로그인 시 이 컬럼으로 캐릭터를 찾고,
  --         없으면 새로 만든 뒤 그 PlayerId를 Redis 티켓에 실어 보냅니다.
  --         C++ GameServer는 이 컬럼을 **읽지 않습니다** (조회는 항상 WHERE PlayerId).
  --         → 게임 서버가 계정 도메인을 모르게 유지하기 위한 의도적 분리입니다.
  --         ⚠️ 향후 정식 로그인 + 캐릭터 선택 UI를 만들 때는 C++도 계정↔캐릭터
  --            관계를 알아야 하므로, 그 시점에 이 분리 전제를 재검토해야 합니다.
  `AccountId` bigint(20)  NOT NULL DEFAULT 0,
  `Name`      varchar(50) NOT NULL,
  `Level`     int(11)     NOT NULL DEFAULT 1,
  `Exp`       int(11)     NOT NULL DEFAULT 0,
  `Hp`        int(11)     NOT NULL DEFAULT 100,
  `ClassId`   int(11)     NOT NULL DEFAULT 0,
  `Gold`      int(11)     NOT NULL DEFAULT 0,
  `PosX`      float       NOT NULL DEFAULT 0,
  `PosY`      float       NOT NULL DEFAULT 0,
  `PosZ`      float       NOT NULL DEFAULT 0,
  `Yaw`       float       NOT NULL DEFAULT 0,
  PRIMARY KEY (`PlayerId`),
  KEY `IX_Player_AccountId` (`AccountId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 기존 DB(AccountId 컬럼이 없던 시절)를 갱신하는 경우를 위한 보정.
-- MySQL 8.0에는 ADD COLUMN IF NOT EXISTS가 없으므로 information_schema로 분기합니다.
SET @has_account_id := (
  SELECT COUNT(*) FROM information_schema.COLUMNS
  WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'Player' AND COLUMN_NAME = 'AccountId'
);
SET @ddl := IF(@has_account_id = 0,
  'ALTER TABLE `Player` ADD COLUMN `AccountId` bigint(20) NOT NULL DEFAULT 0, ADD KEY `IX_Player_AccountId` (`AccountId`)',
  'DO 0');
PREPARE stmt FROM @ddl; EXECUTE stmt; DEALLOCATE PREPARE stmt;

-- ----------------------------------------------------------------------------
--  Mailbox — 우편함 패턴(Phase 3)
--  ※ 과거 mailbox.sql에 있던 Player DDL은 이 파일로 통합되었습니다. [F4]
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS `Mailbox` (
  `MailId`   bigint(20) NOT NULL AUTO_INCREMENT,
  `PlayerId` bigint(20) NOT NULL,
  PRIMARY KEY (`MailId`),
  KEY `IX_Mailbox_PlayerId` (`PlayerId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
