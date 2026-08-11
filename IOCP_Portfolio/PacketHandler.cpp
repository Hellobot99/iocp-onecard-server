#include "Server.h"
#include "PacketHandler.h"
#include "protocol.pb.h"
#include "onecard.pb.h"
#include "OneCardRoom.h"
#include "OneCardQueueManager.h"

extern Server *g_Server;

void PacketHandler::HandlePacket(Session *session, uint16_t id, const char *payload, uint16_t bodySize)
{
    switch (id)
    {
    case 2:
        HandleEnter(session, payload, bodySize);
        break;
    case 4:
        HandleLeave(session, payload, bodySize);
        break;
    case 6:
        HandleChat(session, payload, bodySize);
        break;
    case static_cast<uint16_t>(PacketId::C_OneCardJoinQueue):
        HandleOneCardJoinQueue(session, payload, bodySize);
        break;
    case static_cast<uint16_t>(PacketId::C_OneCardLeaveQueue):
        HandleOneCardLeaveQueue(session, payload, bodySize);
        break;
    case static_cast<uint16_t>(PacketId::C_OneCardPlayCard):
        HandleOneCardPlayCard(session, payload, bodySize);
        break;
    case static_cast<uint16_t>(PacketId::C_OneCardDrawCard):
        HandleOneCardDrawCard(session, payload, bodySize);
        break;
    default:
        return;
    }
}

void PacketHandler::HandleEnter(Session *session, const char *payload, uint16_t bodySize)
{
    g_Server->GetSessionManager().broadcast(session, PacketId::S_Enter, payload, bodySize);
}

void PacketHandler::HandleLeave(Session *session, const char *payload, uint16_t bodySize)
{
    g_Server->GetSessionManager().broadcast(session, PacketId::S_Leave, payload, bodySize);
}

void PacketHandler::HandleChat(Session *session, const char *payload, uint16_t bodySize)
{
    C_Chat recvPkt;
    recvPkt.ParseFromArray(payload, bodySize);

    S_Chat sendPkt;
    sendPkt.set_nickname(session->getNickname());
    sendPkt.set_message(recvPkt.message());

    std::string serialized;
    sendPkt.SerializeToString(&serialized);

    g_Server->GetSessionManager().broadcast(session, PacketId::S_Chat, serialized.c_str(), serialized.size());
}

void PacketHandler::HandleOneCardJoinQueue(Session *session, const char *payload, uint16_t bodySize)
{
    // 이미 게임에 들어가 있으면 큐에 넣지 않는다.
    if (session->GetOneCardRoom())
        return;

    g_Server->GetOneCardQueueManager().Join(session);
}

void PacketHandler::HandleOneCardLeaveQueue(Session *session, const char *payload, uint16_t bodySize)
{
    g_Server->GetOneCardQueueManager().Leave(session);
}

void PacketHandler::HandleOneCardPlayCard(Session *session, const char *payload, uint16_t bodySize)
{
    onecard::C_PlayCard pkt;
    if (!pkt.ParseFromArray(payload, bodySize))
        return;

    std::shared_ptr<OneCardRoom> room = session->GetOneCardRoom();
    if (!room)
        return;

    room->HandlePlayCard(session, pkt.card(), pkt.declared_suit());
}

void PacketHandler::HandleOneCardDrawCard(Session *session, const char *payload, uint16_t bodySize)
{
    std::shared_ptr<OneCardRoom> room = session->GetOneCardRoom();
    if (!room)
        return;

    room->HandleDrawCard(session);
}
