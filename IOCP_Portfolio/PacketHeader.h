#pragma once
#include <cstdint>

struct PacketHeader
{
    uint16_t id;
    uint16_t size;
};

enum class PacketId : uint16_t
{
    S_Enter = 1,
    C_Enter = 2,
    S_Leave = 3,
    C_Leave = 4,
    S_Chat = 5,
    C_Chat = 6,

    // 원카드 - 매칭
    C_OneCardJoinQueue = 7,
    C_OneCardLeaveQueue = 8,
    S_OneCardQueueStatus = 9,
    S_OneCardGameStart = 10,

    // 원카드 - 게임 진행
    C_OneCardPlayCard = 11,
    C_OneCardDrawCard = 12,
    S_OneCardHandUpdate = 13,
    S_OneCardGameState = 14,
    S_OneCardCardPlayed = 15,
    S_OneCardCardDrawn = 16,
    S_OneCardInvalidMove = 17,
    S_OneCardGameOver = 18,

    // 계정 (회원가입/로그인)
    C_SignUp = 19,
    S_SignUpResult = 20,
    C_Login = 21,
    S_LoginResult = 22,

    // 부하테스트용 RTT 측정 (게임 로직과 무관, payload 없음)
    C_Ping = 23,
    S_Pong = 24,
};