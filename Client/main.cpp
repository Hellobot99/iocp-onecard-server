#include "TestClient.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <cstdlib>
#include <mutex>
#include <algorithm>
#include <numeric>

std::atomic<bool> g_running(true);
std::atomic<long long> g_totalSent(0);
std::atomic<long long> g_totalReceived(0);
std::atomic<long long> g_totalConnected(0);
std::atomic<long long> g_totalInvalidMoves(0);
std::atomic<long long> g_totalGamesFinished(0);

// 봇별 RTT(핑) 표본. 표본 수가 많지 않아서(봇당 최대 20개) 그냥 mutex로
// 보호하는 벡터에 다 모았다가 끝나고 한 번에 정렬해서 통계를 낸다.
std::mutex g_latencyMutex;
std::vector<double> g_latenciesMs;

// 사용법: Client.exe [봇 수] [지속 시간(초)] [서버 IP] [포트]
// 기본값: 봇 50개, 60초, 127.0.0.1, 9000.
int main(int argc, char *argv[])
{
    // /utf-8로 빌드해서 문자열 리터럴이 UTF-8인데, cmd 콘솔은 기본 코드페이지가
    // 따로 있어서(보통 949) 그대로 출력하면 한글이 깨진다. 콘솔 출력 코드페이지를
    // UTF-8로 맞춰서 고친다.
    SetConsoleOutputCP(CP_UTF8);

    int clientCount = 50;
    int durationSec = 60;
    std::string serverIp = "127.0.0.1";
    int serverPort = 9000;

    if (argc >= 2)
        clientCount = std::atoi(argv[1]);
    if (argc >= 3)
        durationSec = std::atoi(argv[2]);
    if (argc >= 4)
        serverIp = argv[3];
    if (argc >= 5)
        serverPort = std::atoi(argv[4]);

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::cout << "부하테스트 시작: " << serverIp << ":" << serverPort
              << " 대상, 봇 " << clientCount << "개, " << durationSec << "초 동안" << std::endl;

    std::vector<std::unique_ptr<TestClient>> clients;
    std::vector<std::thread> threads;

    for (int i = 0; i < clientCount; i++)
    {
        clients.push_back(std::make_unique<TestClient>());
        TestClient *client = clients.back().get();
        threads.emplace_back([client, serverIp, serverPort, i]()
                              { client->Start(serverIp, serverPort, i); });

        // 서버 accept() 루프가 한 번에 하나씩 처리하는데, 순간적으로 수백 개를
        // 몰아서 연결하면 리스닝 소켓 backlog에서 밀릴 수 있어 살짝 텀을 둔다.
        Sleep(5);
    }

    std::thread statsThread([]()
                             {
        int elapsed = 0;
        while (g_running)
        {
            Sleep(1000);
            elapsed++;
            std::cout << "[" << elapsed << "s] 연결: " << g_totalConnected
                      << " | 송신: " << g_totalSent
                      << " | 수신: " << g_totalReceived
                      << " | 거부된 행동: " << g_totalInvalidMoves
                      << " | 종료된 게임: " << g_totalGamesFinished
                      << std::endl;
        } });

    Sleep(durationSec * 1000);
    g_running = false;

    for (auto &client : clients)
        client->Stop();

    for (auto &t : threads)
        if (t.joinable())
            t.join();

    statsThread.join();

    std::cout << "\n=== 최종 결과 ===" << std::endl;
    std::cout << "총 연결: " << g_totalConnected << std::endl;
    std::cout << "총 송신: " << g_totalSent << std::endl;
    std::cout << "총 수신: " << g_totalReceived << std::endl;
    std::cout << "서버가 거부한 행동: " << g_totalInvalidMoves << std::endl;
    std::cout << "완료된 게임 수: " << g_totalGamesFinished << std::endl;

    {
        std::lock_guard<std::mutex> lock(g_latencyMutex);
        if (!g_latenciesMs.empty())
        {
            std::sort(g_latenciesMs.begin(), g_latenciesMs.end());
            const size_t n = g_latenciesMs.size();
            const double sum = std::accumulate(g_latenciesMs.begin(), g_latenciesMs.end(), 0.0);

            auto percentile = [&](double p)
            {
                size_t idx = static_cast<size_t>(p * (n - 1));
                return g_latenciesMs[idx];
            };

            std::cout << "\n=== 핑(RTT) 통계 (표본 " << n << "개) ===" << std::endl;
            std::cout << "평균: " << (sum / n) << "ms" << std::endl;
            std::cout << "최소: " << g_latenciesMs.front() << "ms" << std::endl;
            std::cout << "최대: " << g_latenciesMs.back() << "ms" << std::endl;
            std::cout << "p50: " << percentile(0.50) << "ms" << std::endl;
            std::cout << "p95: " << percentile(0.95) << "ms" << std::endl;
            std::cout << "p99: " << percentile(0.99) << "ms" << std::endl;
        }
    }

    WSACleanup();
}
