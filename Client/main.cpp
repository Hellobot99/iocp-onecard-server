#include "TestClient.h"
#include <atomic>

std::atomic<bool>      g_running(true);
std::atomic<int>       g_totalSent(0);
std::atomic<int>       g_totalReceived(0);
std::atomic<long long> g_totalRtt(0);
std::atomic<long long> g_minRtt(LLONG_MAX);
std::atomic<long long> g_maxRtt(0);

int main()
{
    int clientCount = 10;
    int durationSec = 30;
    std::vector<std::thread> threads;

    for (int i = 0; i < clientCount; i++)
    {
        threads.emplace_back([]() {
            TestClient tc(1);
            tc.Start("127.0.0.1", 9000);
        });
    }

    std::thread statsThread([]() {
        int elapsed = 0;
        while (g_running)
        {
            Sleep(1000);
            elapsed++;
            int received = g_totalReceived.load();
            int sent     = g_totalSent.load();
            long long totalRtt = g_totalRtt.load();
            long long minRtt   = g_minRtt.load();
            long long maxRtt   = g_maxRtt.load();

            long long avgRtt = (received > 0) ? totalRtt / received : 0;
            int lossRate = (sent > 0) ? (sent - received) * 100 / sent : 0;

            std::cout << "[" << elapsed << "s]"
                      << " Sent: " << sent
                      << " | Recv: " << received
                      << " | Loss: " << lossRate << "%"
                      << " | RTT avg: " << avgRtt / 1000 << "ms"
                      << " | min: " << (minRtt == LLONG_MAX ? 0 : minRtt / 1000) << "ms"
                      << " | max: " << maxRtt / 1000 << "ms"
                      << std::endl;
        }

        int sent     = g_totalSent.load();
        int received = g_totalReceived.load();
        long long totalRtt = g_totalRtt.load();
        long long minRtt   = g_minRtt.load();
        long long maxRtt   = g_maxRtt.load();
        long long avgRtt   = (received > 0) ? totalRtt / received : 0;
        int lossRate = (sent > 0) ? (sent - received) * 100 / sent : 0;

        std::cout << "\n=== 최종 결과 ===" << std::endl;
        std::cout << "총 송신: " << sent << std::endl;
        std::cout << "총 수신: " << received << std::endl;
        std::cout << "패킷 손실률: " << lossRate << "%" << std::endl;
        std::cout << "평균 RTT: " << avgRtt / 1000 << "ms" << std::endl;
        std::cout << "최소 RTT: " << (minRtt == LLONG_MAX ? 0 : minRtt / 1000) << "ms" << std::endl;
        std::cout << "최대 RTT: " << maxRtt / 1000 << "ms" << std::endl;
    });

    Sleep(durationSec * 1000);
    g_running = false;

    for (auto &t : threads)
        if (t.joinable()) t.join();
    statsThread.join();
}
