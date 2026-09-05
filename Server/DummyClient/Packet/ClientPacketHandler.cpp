#include "ClientPacketHandler.h"
#include "CoreGlobal.h"
#include "JobTimer.h"
#include "Session.h"
#include "ThreadManager.h"
#include <iostream>
#include <thread>
#include <cmath>
#include "../DummyScenario.h"

std::array<PacketHandlerFunc, UINT16_MAX + 1> GPacketHandler; // [S2] 65536칸

bool Handle_INVALID(PacketSessionRef &session, std::span<std::byte> buffer) {
  return false;
}

void DoStressTest(PacketSessionRef session) {
  if (session == nullptr)
    return;

  std::thread([session]() {
    // 메모리/핸들 누수 검증을 위해 접속 중 패킷 전송 후 접속을 종료합니다.
    for (int32 i = 0; i < 5; i++) {
      if (session->IsConnected() == false)
        break;

      Protocol::C_MOVE movePkt;
      movePkt.mutable_posinfo()->set_x(i * 100.0f);
      movePkt.mutable_posinfo()->set_y(i * 100.0f);
      movePkt.mutable_posinfo()->set_z(0.0f);
      movePkt.mutable_posinfo()->set_yaw(0.0f);
      SendBufferRef sendBuffer = ClientPacketHandler::MakeSendBuffer(movePkt);
      session->Send(sendBuffer);

      // 고블린 사냥 시뮬레이션
      Protocol::C_ATTACK attackPkt;
      attackPkt.set_targetid(999);
      session->Send(ClientPacketHandler::MakeSendBuffer(attackPkt));

      // 우편함 수령 시뮬레이션 (DB 비동기 조회)
      Protocol::C_CHECK_MAILBOX mailboxPkt;
      session->Send(ClientPacketHandler::MakeSendBuffer(mailboxPkt));

      // 과부하 방지 및 패킷 처리 여유 시간을 위해 500ms 대기
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 테스트용 통신이 끝나면 연결을 직접 끊어서 리소스 해제(Disconnect) 유도
    if (session->IsConnected()) {
      session->Disconnect(L"Test Lifecycle Complete");
    }
  }).detach();
}

bool Handle_S_LOGIN(PacketSessionRef &session, Protocol::S_LOGIN &pkt) {
  if (pkt.success() == false)
    return true;

  DummyScenario::GLoginOk.store(true);

  /*
   * [2026-09-04] C_ENTER_GAME 송신 추가 — 결함 F12
   *
   * 변경 전: S_LOGIN을 받으면 곧장 DoStressTest()로 C_MOVE/C_ATTACK을 보냈습니다.
   *   그런데 **C_ENTER_GAME을 한 번도 보내지 않았습니다.**
   *   서버는 C_ENTER_GAME을 받아야 Player 객체를 만들고 방에 입장시키므로,
   *   그 전에 도착한 C_MOVE는 Handle_C_MOVE에서 GetPlayer()가 nullptr이라
   *   조용히 return false 되었습니다.
   *   즉 DummyClient는 지금까지 **게임 로직을 한 번도 실행시키지 못했습니다.**
   *   접속·해제 수명주기만 검증했을 뿐, 이동/저장 경로는 미검증 상태였습니다.
   *
   * 변경 후: 로그인 성공 즉시 C_ENTER_GAME을 보내 정상 입장 절차를 밟습니다.
   */
  Protocol::C_ENTER_GAME enterPkt;
  session->Send(ClientPacketHandler::MakeSendBuffer(enterPkt));

  if (DummyScenario::GMode == DummyScenario::Mode::Stress)
    DoStressTest(session);

  return true;
}

/*
 * Handle_S_ENTER_GAME
 * [2026-09-04] 시나리오 판정의 핵심 지점입니다.
 *   서버가 DB에서 읽어 보낸 좌표가 여기 실려 옵니다. UE 클라이언트가 아니라
 *   이 콘솔 도구로도 왕복(저장 → 복원)을 검증할 수 있게 되었습니다.
 */
bool Handle_S_ENTER_GAME(PacketSessionRef &session,
                         Protocol::S_ENTER_GAME &pkt) {
  if (pkt.success() == false) {
    std::cout << "[DummyClient] S_ENTER_GAME failed." << std::endl;
    DummyScenario::GFinished.store(true);
    return true;
  }

  const auto &obj = pkt.player().objectinfo();
  const auto &pos = obj.posinfo();

  DummyScenario::GEnterOk.store(true);
  DummyScenario::GReceivedPlayerId.store(obj.objectid());
  DummyScenario::GRecvX.store(static_cast<int32>(std::lround(pos.x())));
  DummyScenario::GRecvY.store(static_cast<int32>(std::lround(pos.y())));
  DummyScenario::GRecvZ.store(static_cast<int32>(std::lround(pos.z())));

  std::cout << "[DummyClient] S_ENTER_GAME  PlayerId=" << obj.objectid()
            << "  Name=" << obj.name() << "  Level=" << pkt.player().level()
            << "  Pos=(" << pos.x() << ", " << pos.y() << ", " << pos.z() << ")"
            << std::endl;

  switch (DummyScenario::GMode) {
  case DummyScenario::Mode::Move: {
    // 지정 좌표로 이동시켜 서버 상태를 갱신한 뒤 정상 종료합니다.
    // 정상 종료 시 GameRoom::Leave가 스냅샷을 DB에 저장(UPSERT)합니다.
    Protocol::C_MOVE movePkt;
    movePkt.mutable_posinfo()->set_x(DummyScenario::GTargetX);
    movePkt.mutable_posinfo()->set_y(DummyScenario::GTargetY);
    movePkt.mutable_posinfo()->set_z(DummyScenario::GTargetZ);
    movePkt.mutable_posinfo()->set_yaw(0.0f);
    /*
     * [2026-09-06 수정] 1회 송신 후 고정 대기 → **에코 확인까지 재전송**
     *
     * 이전: Send 1회 → 800ms sleep → Disconnect.
     *   서버가 S_ENTER_GAME 을 선발송한 뒤 GameRoom::Enter 를 큐에 넣기 때문에,
     *   이 시점의 C_MOVE 는 player->GetRoom() 이 아직 nullptr 이라
     *   Handle_C_MOVE 에서 **로그 없이 버려질 수 있습니다.**
     *   그러면 DB 에 (0,0,0) 이 저장되고 이후 verify 가 거짓 실패합니다.
     *
     * 이후: S_MOVE 에코(=서버가 실제로 반영했다는 증거)를 받을 때까지 재전송합니다.
     */
    std::thread([session]() {
      Protocol::C_MOVE movePkt;
      movePkt.mutable_posinfo()->set_x(DummyScenario::GTargetX);
      movePkt.mutable_posinfo()->set_y(DummyScenario::GTargetY);
      movePkt.mutable_posinfo()->set_z(DummyScenario::GTargetZ);
      movePkt.mutable_posinfo()->set_yaw(0.0f);

      // 최대 20회(약 4초) 재시도. 입장 직후의 짧은 창만 넘기면 되므로 충분합니다.
      for (int32 attempt = 1; attempt <= 20; attempt++) {
        if (DummyScenario::GMoveAcked.load() || session->IsConnected() == false)
          break;

        session->Send(ClientPacketHandler::MakeSendBuffer(movePkt));
        if (attempt == 1) {
          std::cout << "[DummyClient] C_MOVE sent -> ("
                    << DummyScenario::GTargetX << ", " << DummyScenario::GTargetY
                    << ", " << DummyScenario::GTargetZ << ")" << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }

      if (DummyScenario::GMoveAcked.load())
        std::cout << "[DummyClient] C_MOVE ACKED (server applied)" << std::endl;
      else
        std::cout << "[DummyClient] C_MOVE NOT acked — 서버가 반영하지 않았습니다"
                  << std::endl;

      // 반영 확인 후에도 서버의 Leave 스냅샷 저장이 돌 시간을 조금 줍니다.
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
      if (session->IsConnected())
        session->Disconnect(L"Scenario Move Complete");
      DummyScenario::GFinished.store(true);
    }).detach();
    break;
  }
  case DummyScenario::Mode::Verify: {
    const float dx = std::fabs(pos.x() - DummyScenario::GExpectX);
    const float dy = std::fabs(pos.y() - DummyScenario::GExpectY);
    const float dz = std::fabs(pos.z() - DummyScenario::GExpectZ);
    const bool ok = (dx <= DummyScenario::GTolerance) &&
                    (dy <= DummyScenario::GTolerance) &&
                    (dz <= DummyScenario::GTolerance);

    DummyScenario::GPassed.store(ok);
    std::cout << "[DummyClient] VERIFY expect=(" << DummyScenario::GExpectX << ", "
              << DummyScenario::GExpectY << ", " << DummyScenario::GExpectZ
              << ")  actual=(" << pos.x() << ", " << pos.y() << ", " << pos.z()
              << ")  => " << (ok ? "PASS" : "FAIL") << std::endl;

    std::thread([session]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (session->IsConnected())
        session->Disconnect(L"Scenario Verify Complete");
      DummyScenario::GFinished.store(true);
    }).detach();
    break;
  }
  case DummyScenario::Mode::Idle:
  default:
    break;
  }

  return true;
}
bool Handle_S_LEAVE_GAME(PacketSessionRef &session,
                         Protocol::S_LEAVE_GAME &pkt) {
  return true;
}
bool Handle_S_SPAWN(PacketSessionRef &session, Protocol::S_SPAWN &pkt) {
  return true;
}
bool Handle_S_DESPAWN(PacketSessionRef &session, Protocol::S_DESPAWN &pkt) {
  return true;
}
bool Handle_S_MOVE(PacketSessionRef &session, Protocol::S_MOVE &pkt) {
  /*
   * [2026-09-06] Move 시나리오의 "서버 반영 확인" 신호.
   *
   * 서버는 이동을 AOI 반경으로 브로드캐스트하는데, 이동한 본인은 언제나 자기
   * 반경 안에 있으므로 **자기 이동의 에코를 자신도 받습니다.**
   * 이 에코가 곧 "서버가 HandleMove 를 실행해 상태를 갱신했다"는 증거이므로,
   * 이것을 받아야 비로소 이동이 성립했다고 판정합니다.
   */
  if (DummyScenario::GMode == DummyScenario::Mode::Move &&
      DummyScenario::GMoveAcked.load() == false) {
    const auto &pos = pkt.posinfo();
    const float dx = std::fabs(pos.x() - DummyScenario::GTargetX);
    const float dy = std::fabs(pos.y() - DummyScenario::GTargetY);
    const float dz = std::fabs(pos.z() - DummyScenario::GTargetZ);

    // 목표 좌표와 일치하는 에코만 인정합니다.
    // (다른 플레이어의 이동 브로드캐스트를 자기 것으로 오인하지 않기 위함)
    if (dx <= DummyScenario::GTolerance && dy <= DummyScenario::GTolerance &&
        dz <= DummyScenario::GTolerance) {
      DummyScenario::GMoveAcked.store(true);
    }
  }
  return true;
}
bool Handle_S_CHAT(PacketSessionRef &session, Protocol::S_CHAT &pkt) {
  return true;
}
bool Handle_S_PONG(PacketSessionRef &session, Protocol::S_PONG &pkt) {
  return true;
}

bool Handle_S_ATTACK(PacketSessionRef &session, Protocol::S_ATTACK &pkt) {
  return true;
}

bool Handle_S_STATUS_CHANGE(PacketSessionRef &session,
                            Protocol::S_STATUS_CHANGE &pkt) {
  return true;
}