-- ============================================================================
--  테스트 계정 시드 (Deterministic Test Identity)
-- ============================================================================
--  [왜 필요한가 — 결함 ID F7, 1번째 겹]
--    과거 UE 클라이언트는 접속할 때마다 `DummyTicket_<난수>`를 스스로 만들었고,
--    서버는 접속마다 idGenerator.fetch_add(1)로 새 PlayerId를 발급했습니다.
--    그래서 **같은 사람으로 두 번 로그인하는 것이 구조적으로 불가능**했고,
--    "저장했다가 다시 불러온다"는 왕복을 시험할 수 없었습니다.
--    실제 계정으로 로그인하게 되면서, 테스트 신원이 비로소 결정적(deterministic)이 됩니다.
--
--  [임포트 순서 — 반드시 지킬 것]
--    1) schema.sql                 : Player / Mailbox (C++ 정본)
--    2) WebBackend 최초 1회 기동    : Account 테이블 자동 생성
--                                    (Program.cs가 시작 시 EF Migration을 적용합니다.
--                                     EC2에 dotnet-ef CLI를 설치할 필요가 없습니다.)
--    3) seed_accounts.sql          : 이 파일
--
--  [비밀번호에 대하여]
--    현재 평문 비교입니다. AuthController.cs 주석에도 명시되어 있습니다.
--    데모 범위에서는 그대로 두되, **Production 전 해싱 도입은 필수**이며
--    AI_HANDOFF.md에 인계 항목으로 기록되어 있습니다.
--
--  [계정 추가 방법]
--    아래 VALUES 목록에 한 줄을 더하고 이 파일을 다시 임포트하면 됩니다.
--    INSERT IGNORE이므로 몇 번을 다시 실행해도 안전합니다(멱등).
--    UE 클라이언트에서 그 계정을 쓰려면:
--      · PIE 기본 계정  → AMC1GameInstance의 AccountName/Password 기본값을 바꾸거나
--                          DefaultGame.ini의 [/Script/AMC1.AMC1GameInstance] 섹션에 지정
--      · 동시 접속 시험 → 실행 인수로 지정  -AccountName=test03 -Password=test03
--    캐릭터(Player 행)는 미리 만들 필요가 없습니다.
--    최초 로그인 시 C# AuthController가 그 계정의 캐릭터를 자동 생성합니다.
-- ============================================================================

INSERT IGNORE INTO `Account` (`AccountName`, `Password`) VALUES
  ('test01', 'test01'),   -- PIE 기본 계정 (AMC1GameInstance UPROPERTY 기본값과 일치시킬 것)
  ('test02', 'test02'),   -- 2인 동시 접속 / 상호 관측 시험용
  ('test03', 'test03'),   -- 예비
  ('test04', 'test04'),   -- 예비
  ('test05', 'test05');   -- 예비
  -- ↑ 여기에 계속 추가하십시오. 형식: ('계정명', '비밀번호'),

-- 확인용 조회 (임포트 후 실행해 보십시오)
--   SELECT a.AccountId, a.AccountName, p.PlayerId, p.Name, p.Gold, p.PosX, p.PosY, p.PosZ
--   FROM Account a LEFT JOIN Player p ON p.AccountId = a.AccountId
--   ORDER BY a.AccountId;
