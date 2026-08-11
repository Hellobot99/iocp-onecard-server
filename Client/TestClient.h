#pragma once

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

#include <vector>
#include <thread>
#include <string>
#include <iostream>
#include <atomic>
#include <chrono>
#include <climits>

#pragma comment(lib, "ws2_32.lib")

class TestClient
{
public:
    TestClient(int ThreadCount);
    ~TestClient();
    void Start(std::string ip, int port);
    void Stop();
    void Send();
    void Recv();

private:
    bool running_ = false;
    std::vector<std::thread> ioThreads_;
    SOCKET sock_;
};