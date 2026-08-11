#include "TestClient.h"
#include "PacketHeader.h"
#include "protocol.pb.h"

extern std::atomic<bool> g_running;
extern std::atomic<long long> g_totalRtt;
extern std::atomic<int> g_totalReceived;
extern std::atomic<int> g_totalSent;
extern std::atomic<long long> g_minRtt;
extern std::atomic<long long> g_maxRtt;

TestClient::TestClient(int ThreadCount)
{
}

TestClient::~TestClient()
{
    Stop();
}

void TestClient::Start(std::string ip, int port)
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    connect(sock_, (sockaddr *)&addr, sizeof(addr));

    running_ = true;

    ioThreads_.emplace_back([this]() { Send(); });
    ioThreads_.emplace_back([this]() { Recv(); });

    for (auto &t : ioThreads_)
        if (t.joinable())
            t.join();
}

void TestClient::Stop()
{
    running_ = false;

    for (auto &t : ioThreads_)
        if (t.joinable())
            t.join();
}

void TestClient::Send()
{
    while (g_running)
    {
        auto now = std::chrono::high_resolution_clock::now();
        long long timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();

        C_Chat pkt;
        pkt.set_message(std::to_string(timestamp));

        std::string serialized;
        pkt.SerializeToString(&serialized);

        PacketHeader header;
        header.id = static_cast<uint16_t>(PacketId::C_Chat);
        header.size = sizeof(PacketHeader) + serialized.size();

        std::vector<char> packet(sizeof(PacketHeader) + serialized.size());
        memcpy(packet.data(), &header, sizeof(PacketHeader));
        memcpy(packet.data() + sizeof(PacketHeader), serialized.c_str(), serialized.size());

        send(sock_, packet.data(), packet.size(), 0);
        g_totalSent++;

        Sleep(1000);
    }
}

void TestClient::Recv()
{
    while (g_running)
    {
        char buf[1024];
        int len = recv(sock_, buf, 1024, 0);
        if (len <= 0) break;

        S_Chat pkt;
        pkt.ParseFromArray(buf + sizeof(PacketHeader), len - sizeof(PacketHeader));

        try {
            long long sentTime = std::stoll(pkt.message());
            auto now = std::chrono::high_resolution_clock::now();
            long long recvTime = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            long long rtt = recvTime - sentTime;

            g_totalRtt += rtt;
            g_totalReceived++;

            long long curMin = g_minRtt.load();
            while (rtt < curMin && !g_minRtt.compare_exchange_weak(curMin, rtt));

            long long curMax = g_maxRtt.load();
            while (rtt > curMax && !g_maxRtt.compare_exchange_weak(curMax, rtt));
        }
        catch (...) {}
    }
}
