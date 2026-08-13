#include "Server.h"
#include "PacketHandler.h"
#include "protocol.pb.h"
#include "onecard.pb.h"
#include "auth.pb.h"
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
    case static_cast<uint16_t>(PacketId::C_SignUp):
        HandleSignUp(session, payload, bodySize);
        break;
    case static_cast<uint16_t>(PacketId::C_Login):
        HandleLogin(session, payload, bodySize);
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
    // 로그인 전에는 매칭에 들어갈 수 없다. PlayCard/DrawCard는 방이 있어야만
    // 동작하고 방은 여기(JoinQueue)를 통과해야만 생기니, 여기 하나만 막아도
    // 게임 흐름 전체가 로그인 여부로 걸러진다.
    if (!session->IsAuthenticated())
        return;

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

void PacketHandler::HandleSignUp(Session *session, const char *payload, uint16_t bodySize)
{
    auth::C_SignUp pkt;
    if (!pkt.ParseFromArray(payload, bodySize))
        return;

    std::string username = pkt.username();
    std::string password = pkt.password();
    uint64_t generation = session->GetGeneration();

    // DB 조회(파일 I/O)는 JobQueue 워커 스레드에서 실행한다 - 여기(IOCP 스레드)에서
    // 직접 하면 그 시간 동안 이 스레드가 처리해야 할 다른 클라이언트 패킷이 밀린다.
    g_Server->GetJobQueue().Push([session, generation, username, password]()
                                  {
        SignUpResult result = g_Server->GetGameDB().SignUp(username, password);

        // 쿼리가 끝나기 전에 연결이 끊기고 이 Session 객체가 다른 연결에
        // 재사용됐으면, 응답을 그 엉뚱한 사람에게 보내면 안 되니 버린다.
        if (session->GetGeneration() != generation)
            return;

        auth::S_SignUpResult msg;
        msg.set_success(result.success);
        msg.set_message(result.message);

        std::string body;
        msg.SerializeToString(&body);
        session->Send(PacketId::S_SignUpResult, body.data(), static_cast<DWORD>(body.size())); });
}

void PacketHandler::HandleLogin(Session *session, const char *payload, uint16_t bodySize)
{
    auth::C_Login pkt;
    if (!pkt.ParseFromArray(payload, bodySize))
        return;

    std::string username = pkt.username();
    std::string password = pkt.password();
    uint64_t generation = session->GetGeneration();

    g_Server->GetJobQueue().Push([session, generation, username, password]()
                                  {
        LoginResult result = g_Server->GetGameDB().Login(username, password);

        if (session->GetGeneration() != generation)
            return;

        if (result.success)
            session->SetAuthenticated(username);

        auth::S_LoginResult msg;
        msg.set_success(result.success);
        msg.set_message(result.message);
        msg.set_gold(result.gold);

        std::string body;
        msg.SerializeToString(&body);
        session->Send(PacketId::S_LoginResult, body.data(), static_cast<DWORD>(body.size())); });
}
