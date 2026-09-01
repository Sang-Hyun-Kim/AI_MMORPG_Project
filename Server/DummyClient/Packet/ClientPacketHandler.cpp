#include "ClientPacketHandler.h"
#include "CoreGlobal.h"
#include "JobTimer.h"
#include "Session.h"
#include "ThreadManager.h"
#include <iostream>
#include <thread>

std::array<PacketHandlerFunc, UINT16_MAX> GPacketHandler;

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
  if (pkt.success()) {
    // std::cout << "[DummyClient] Login Success! Starting Stress Test..." <<
    // std::endl;
    DoStressTest(session);
  }
  return true;
}
bool Handle_S_ENTER_GAME(PacketSessionRef &session,
                         Protocol::S_ENTER_GAME &pkt) {
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
  // std::cout << "[DummyClient] Echo S_MOVE Received - Name: " <<
  // pkt.info().name() << ", Level: " << pkt.info().level() << std::endl;
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