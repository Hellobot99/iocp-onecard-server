#pragma once

// windows.h가 min/max를 매크로로 정의해서 std::min/std::max와 충돌하는 걸 막는다.
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <vector>
#include <string>
#include <iostream>
#include <atomic>
#include <google/protobuf/message.h>
#include "onecard.pb.h"
#include "auth.pb.h"

#pragma comment(lib, "ws2_32.lib")

// 원카드 서버에 접속해서 회원가입/로그인 -> 매칭 대기열 참가 -> 카드 플레이/
// 드로우까지 자동으로 진행하는 부하테스트용 봇 클라이언트. 서버
// (Session::DispatchPackets)와 동일한 방식으로 recv 버퍼를 누적해서 패킷을
// 재조립한다 (한 번의 recv가 패킷 하나와 대응한다고 가정하면 안 됨).
class TestClient
{
public:
    // botId는 계정 아이디를 만드는 데 쓴다 ("bot0", "bot1", ...). 이미 존재하는
    // 계정이면 회원가입은 실패하지만 그대로 로그인을 시도해서 넘어간다 - DB
    // 파일이 이전 테스트에서 남아있어도 재실행이 항상 되게 하기 위함.
    void Start(const std::string &ip, int port, int botId);
    void Stop();

private:
    void RecvLoop();
    void DispatchPacket(uint16_t id, const char *payload, uint16_t bodySize);

    void SendSignUp();
    void SendLogin();
    void SendJoinQueue();
    void SendPlayCard(const onecard::Card &card, onecard::Suit declaredSuit);
    void SendDrawCard();
    void SendPacket(uint16_t id, const google::protobuf::Message &msg);

    // 내 턴이면 낼 수 있는 카드를 찾아서 내고, 없으면 드로우한다. 서버
    // (OneCardRoom::IsCardPlayable)와 같은 규칙을 따르는 클라이언트 쪽 판단이다.
    void TryTakeTurn();
    bool IsCardPlayable(const onecard::Card &card) const;
    onecard::Suit GuessBestSuit(const onecard::Card &excluding) const;

    SOCKET sock_ = INVALID_SOCKET;
    bool running_ = false;
    std::vector<char> recvBuffer_;

    std::string username_;
    std::string password_ = "loadtest1234"; // 부하테스트 전용 고정 비밀번호 - 실제 계정 아님.

    // 이 봇이 파악하고 있는 최소한의 게임 상태.
    int mySeat_ = -1;
    bool isGameActive_ = false;
    std::vector<onecard::Card> myHand_;
    onecard::Card topCard_;
    onecard::Suit currentSuit_ = onecard::SUIT_NONE;
    int currentTurnSeat_ = -1;
    int pendingDraw_ = 0;
};
