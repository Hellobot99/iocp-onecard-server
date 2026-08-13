#pragma once
#include <cstdint>

struct PacketHeader
{
    uint16_t id;
    uint16_t size;
};

// 서버(IOCP_Portfolio/PacketHeader.h)와 값이 반드시 일치해야 한다.
enum class PacketId : uint16_t
{
    S_Enter = 1,
    C_Enter = 2,
    S_Leave = 3,
    C_Leave = 4,
    S_Chat = 5,
    C_Chat = 6,

    C_OneCardJoinQueue = 7,
    C_OneCardLeaveQueue = 8,
    S_OneCardQueueStatus = 9,
    S_OneCardGameStart = 10,

    C_OneCardPlayCard = 11,
    C_OneCardDrawCard = 12,
    S_OneCardHandUpdate = 13,
    S_OneCardGameState = 14,
    S_OneCardCardPlayed = 15,
    S_OneCardCardDrawn = 16,
    S_OneCardInvalidMove = 17,
    S_OneCardGameOver = 18,

    C_SignUp = 19,
    S_SignUpResult = 20,
    C_Login = 21,
    S_LoginResult = 22,
};
