#pragma once

#include <thread>
#include <memory>

namespace httplib
{
    class Server;
}

// 상점/인벤토리용 REST API. cpp-httplib을 얹어서 자체 스레드에서 돈다.
// 요청 핸들러 안에서 DB가 필요하면 Server::GetJobQueue()에 작업을 넣고
// std::future로 동기 대기한다 - TCP 게임 로직과 동일한 JobQueue를 거치기
// 때문에 SQLite에 동시에 쓰는 경로가 하나로 합쳐진다.
//
// httplib.h는 무거운 싱글 헤더라서 HttpServer.h에는 안 끌어오고(전방 선언만),
// .cpp에서만 include한다.
class HttpServer
{
public:
    HttpServer();
    ~HttpServer();

    void Start(int port);
    void Stop();

private:
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
};
