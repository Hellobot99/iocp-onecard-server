#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <iostream>
#include "SessionManager.h"
#include "PacketHandler.h"
#include "ObjectPool.h"
#include "OneCardQueueManager.h"
#include "JobQueue.h"
#include "GameDB.h"
#include "HttpServer.h"

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
    JobQueue &GetJobQueue() { return jobQueue_; }
    GameDB &GetGameDB() { return gameDB_; }

private:
    SOCKET listenSocket_;
    std::vector<std::thread> ioThreads_;
    std::thread matchmakingThread_;
    int ioThreadCount_;
    bool running_ = false;
    // Stop()이 Ctrl+C 핸들러 스레드와 main 스레드(소멸자)에서 동시에 불릴 수 있다.
    // 락 없이는 한쪽이 스레드 join을 끝내기 전에 다른 쪽이 먼저 통과해서 Server
    // 객체(스레드 멤버 포함)가 소멸돼버리는 race가 생긴다 (아직 join 안 된
    // std::thread가 소멸되면 std::terminate 호출됨).
    std::mutex stopMutex_;
    HANDLE iocpHandle_;
    ObjectPool<Session> sessionPool_;
    ObjectPool<Buffer> bufferPool_;

    SessionManager sessionManager_;
    OneCardQueueManager oneCardQueueManager_;

    // 블로킹될 수 있는 작업(DB 쿼리 등)을 IOCP 워커 스레드 밖에서 처리하기 위한
    // 전용 워커 스레드 큐. gameDB_는 이 큐의 워커 스레드에서만 접근한다 - TCP
    // 게임 로직(PacketHandler)과 HTTP 상점 API(HttpServer) 둘 다 이 큐를 거쳐서
    // 호출하기 때문에 SQLite 동시 접근 문제가 생기지 않는다.
    JobQueue jobQueue_;
    GameDB gameDB_;

    // 상점/인벤토리용 REST API. 실시간 게임 로직(TCP)과는 별개로, 요청-응답
    // 한 방이면 끝나는 트랜잭션은 HTTP가 더 자연스럽다.
    HttpServer httpServer_;
};
