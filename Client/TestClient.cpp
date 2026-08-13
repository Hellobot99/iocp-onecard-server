#include "TestClient.h"
#include "PacketHeader.h"
#include <cstring>

extern std::atomic<bool> g_running;
extern std::atomic<long long> g_totalSent;
extern std::atomic<long long> g_totalReceived;
extern std::atomic<long long> g_totalConnected;
extern std::atomic<long long> g_totalInvalidMoves;
extern std::atomic<long long> g_totalGamesFinished;

namespace
{
    constexpr size_t kBufferCapacity = 1024; // 서버 Buffer::data와 동일한 크기.
}

void TestClient::Start(const std::string &ip, int port, int botId)
{
    username_ = "bot" + std::to_string(botId);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == INVALID_SOCKET)
    {
        std::cerr << "[TestClient] socket() 실패: " << WSAGetLastError() << std::endl;
        return;
    }

    if (connect(sock_, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        // 조용히 실패하면 부하테스트가 "연결 0"으로만 보이고 원인을 알 수 없어서
        // 에러 코드를 찍는다. WSAETIMEDOUT(10060)/WSAECONNREFUSED(10061)가 흔하다.
        std::cerr << "[TestClient] connect(" << ip << ":" << port << ") 실패: " << WSAGetLastError() << std::endl;
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return;
    }

    running_ = true;
    g_totalConnected++;

    SendSignUp();
    RecvLoop();

    if (sock_ != INVALID_SOCKET)
    {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
}

void TestClient::Stop()
{
    running_ = false;

    // recv()에서 블로킹 중인 RecvLoop 스레드를 깨우기 위해 소켓을 강제로 닫는다.
    if (sock_ != INVALID_SOCKET)
        closesocket(sock_);
}

void TestClient::RecvLoop()
{
    char chunk[4096];

    while (running_ && g_running)
    {
        int n = recv(sock_, chunk, sizeof(chunk), 0);
        if (n <= 0)
            break; // 서버가 연결을 끊었거나 소켓 오류.

        recvBuffer_.insert(recvBuffer_.end(), chunk, chunk + n);

        size_t offset = 0;
        bool corrupted = false;

        while (recvBuffer_.size() - offset >= sizeof(PacketHeader))
        {
            PacketHeader header;
            memcpy(&header, recvBuffer_.data() + offset, sizeof(PacketHeader));

            if (header.size < sizeof(PacketHeader) || header.size > kBufferCapacity)
            {
                std::cerr << "[TestClient] invalid packet size(" << header.size << "), dropping stream." << std::endl;
                recvBuffer_.clear();
                corrupted = true;
                break;
            }

            if (recvBuffer_.size() - offset < header.size)
                break; // 패킷이 아직 다 도착하지 않음 - 다음 recv를 기다린다.

            const char *payload = recvBuffer_.data() + offset + sizeof(PacketHeader);
            const uint16_t bodySize = header.size - static_cast<uint16_t>(sizeof(PacketHeader));

            DispatchPacket(header.id, payload, bodySize);
            g_totalReceived++;

            offset += header.size;
        }

        if (!corrupted && offset > 0)
            recvBuffer_.erase(recvBuffer_.begin(), recvBuffer_.begin() + offset);
    }
}

void TestClient::DispatchPacket(uint16_t id, const char *payload, uint16_t bodySize)
{
    switch (static_cast<PacketId>(id))
    {
    case PacketId::S_SignUpResult:
        // 이미 만들어진 계정이라 실패해도 상관없다 (이전 테스트에서 만든 계정을
        // 재사용하는 것뿐) - 성공/실패 상관없이 바로 로그인을 시도한다.
        SendLogin();
        break;

    case PacketId::S_LoginResult:
    {
        auth::S_LoginResult msg;
        msg.ParseFromArray(payload, bodySize);
        if (msg.success())
        {
            SendJoinQueue();
        }
        else
        {
            std::cerr << "[TestClient] " << username_ << " 로그인 실패: " << msg.message() << std::endl;
        }
        break;
    }

    case PacketId::S_OneCardGameStart:
    {
        onecard::S_GameStart msg;
        msg.ParseFromArray(payload, bodySize);
        mySeat_ = msg.your_seat();
        isGameActive_ = true;
        myHand_.clear();
        break;
    }
    case PacketId::S_OneCardHandUpdate:
    {
        onecard::S_HandUpdate msg;
        msg.ParseFromArray(payload, bodySize);
        myHand_.assign(msg.hand().begin(), msg.hand().end());
        break;
    }
    case PacketId::S_OneCardGameState:
    {
        onecard::S_GameState msg;
        msg.ParseFromArray(payload, bodySize);
        topCard_ = msg.top_card();
        currentSuit_ = msg.current_suit();
        currentTurnSeat_ = msg.current_turn_seat();
        pendingDraw_ = msg.pending_draw();

        TryTakeTurn();
        break;
    }
    case PacketId::S_OneCardInvalidMove:
        g_totalInvalidMoves++;
        break;

    case PacketId::S_OneCardGameOver:
        isGameActive_ = false;
        myHand_.clear();
        g_totalGamesFinished++;
        // 게임이 끝나면 다시 대기열에 줄 서서 계속 부하를 만든다.
        SendJoinQueue();
        break;

    default:
        // S_OneCardQueueStatus, S_OneCardCardPlayed, S_OneCardCardDrawn 등은
        // 부하테스트 로직에 영향을 안 줘서 그냥 무시한다.
        break;
    }
}

void TestClient::TryTakeTurn()
{
    if (!isGameActive_ || mySeat_ < 0 || currentTurnSeat_ != mySeat_)
        return;

    for (const onecard::Card &card : myHand_)
    {
        if (!IsCardPlayable(card))
            continue;

        onecard::Suit declaredSuit = onecard::SUIT_NONE;
        if (card.rank() == onecard::JACK)
            declaredSuit = card.suit(); // J는 자기 무늬를 그대로 유지.
        else if (card.rank() == onecard::JOKER)
            declaredSuit = GuessBestSuit(card);

        SendPlayCard(card, declaredSuit);
        return;
    }

    // 낼 수 있는 카드가 없으면 드로우한다.
    SendDrawCard();
}

bool TestClient::IsCardPlayable(const onecard::Card &card) const
{
    if (pendingDraw_ > 0)
        return card.rank() == onecard::TWO;

    if (card.rank() == onecard::JOKER)
        return true;

    return card.suit() == currentSuit_ || card.rank() == topCard_.rank();
}

onecard::Suit TestClient::GuessBestSuit(const onecard::Card &excluding) const
{
    int counts[5] = {0, 0, 0, 0, 0}; // index = Suit enum 값 (SUIT_NONE=0 ~ CLUB=4)

    for (const onecard::Card &c : myHand_)
    {
        bool sameCard = (c.suit() == excluding.suit() && c.rank() == excluding.rank());
        if (sameCard || c.suit() == onecard::SUIT_NONE)
            continue;
        counts[c.suit()]++;
    }

    onecard::Suit best = onecard::SPADE;
    int bestCount = -1;
    for (int s = onecard::SPADE; s <= onecard::CLUB; s++)
    {
        if (counts[s] > bestCount)
        {
            bestCount = counts[s];
            best = static_cast<onecard::Suit>(s);
        }
    }
    return best;
}

void TestClient::SendSignUp()
{
    auth::C_SignUp msg;
    msg.set_username(username_);
    msg.set_password(password_);
    SendPacket(static_cast<uint16_t>(PacketId::C_SignUp), msg);
}

void TestClient::SendLogin()
{
    auth::C_Login msg;
    msg.set_username(username_);
    msg.set_password(password_);
    SendPacket(static_cast<uint16_t>(PacketId::C_Login), msg);
}

void TestClient::SendJoinQueue()
{
    onecard::C_JoinQueue msg;
    SendPacket(static_cast<uint16_t>(PacketId::C_OneCardJoinQueue), msg);
}

void TestClient::SendPlayCard(const onecard::Card &card, onecard::Suit declaredSuit)
{
    onecard::C_PlayCard msg;
    *msg.mutable_card() = card;
    msg.set_declared_suit(declaredSuit);
    SendPacket(static_cast<uint16_t>(PacketId::C_OneCardPlayCard), msg);
}

void TestClient::SendDrawCard()
{
    onecard::C_DrawCard msg;
    SendPacket(static_cast<uint16_t>(PacketId::C_OneCardDrawCard), msg);
}

void TestClient::SendPacket(uint16_t id, const google::protobuf::Message &msg)
{
    std::string body;
    msg.SerializeToString(&body);

    if (sizeof(PacketHeader) + body.size() > kBufferCapacity)
        return; // 서버 쪽 제약(1024바이트)과 동일하게 방어.

    PacketHeader header;
    header.id = id;
    header.size = static_cast<uint16_t>(sizeof(PacketHeader) + body.size());

    std::vector<char> packet(header.size);
    memcpy(packet.data(), &header, sizeof(PacketHeader));
    memcpy(packet.data() + sizeof(PacketHeader), body.data(), body.size());

    send(sock_, packet.data(), static_cast<int>(packet.size()), 0);
    g_totalSent++;
}
