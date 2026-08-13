#pragma once
#include <cstdint>

class Server;
class Session;

class PacketHandler
{
public:
    static void HandlePacket(Session *session, uint16_t id, const char *payload, uint16_t bodySize);

private:
    static void HandleEnter(Session *session, const char *payload, uint16_t bodySize);
    static void HandleLeave(Session *session, const char *payload, uint16_t bodySize);
    static void HandleChat(Session *session, const char *payload, uint16_t bodySize);

    static void HandleOneCardJoinQueue(Session *session, const char *payload, uint16_t bodySize);
    static void HandleOneCardLeaveQueue(Session *session, const char *payload, uint16_t bodySize);
    static void HandleOneCardPlayCard(Session *session, const char *payload, uint16_t bodySize);
    static void HandleOneCardDrawCard(Session *session, const char *payload, uint16_t bodySize);

    static void HandleSignUp(Session *session, const char *payload, uint16_t bodySize);
    static void HandleLogin(Session *session, const char *payload, uint16_t bodySize);
};