#pragma once

#include <vector>
#include <thread>
#include <iostream>
#include "SessionManager.h"
#include "PacketHandler.h"
#include "ObjectPool.h"
#include "OneCardQueueManager.h"

#pragma comment(lib, "ws2_32.lib")

class Server
{
public:
    Server(int ioThreadCount);
    ~Server();
    void Start(int port);
    void Stop();
    void IoThreadWorkerLoop();
    void MatchmakingWorkerLoop();
    SessionManager &GetSessionManager() { return sessionManager_; }
    OneCardQueueManager &GetOneCardQueueManager() { return oneCardQueueManager_; }

private:
    SOCKET listenSocket_;
    std::vector<std::thread> ioThreads_;
    std::thread matchmakingThread_;
    int ioThreadCount_;
    bool running_ = false;
    HANDLE iocpHandle_;
    ObjectPool<Session> sessionPool_;
    ObjectPool<Buffer> bufferPool_;

    SessionManager sessionManager_;
    OneCardQueueManager oneCardQueueManager_;
};
