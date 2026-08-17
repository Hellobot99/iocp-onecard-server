#include "Server.h"
#include "OneCardRoom.h"
#include <chrono>

extern Server *g_Server;

Server::Server(int ioThreadCount)
    : ioThreadCount_(ioThreadCount)
{
    running_ = true;
}

Server::~Server()
{
    Stop();
}

void Server::Start(int port)
{
    g_Server = this;
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    iocpHandle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, ioThreadCount_);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    listenSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket_ == INVALID_SOCKET)
    {
        std::cerr << "[Server] socket() 실패: " << WSAGetLastError() << std::endl;
        return;
    }

    if (bind(listenSocket_, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        std::cerr << "[Server] bind() 실패: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket_);
        return;
    }

    if (listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "[Server] listen() 실패: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket_);
        return;
    }

    if (!gameDB_.Open("game.db"))
    {
        std::cerr << "[Server] 게임 DB를 열지 못했습니다." << std::endl;
        closesocket(listenSocket_);
        return;
    }

    // DB 쿼리는 여기 워커 스레드에서만 실행된다. 워커 1개로 충분하다 - 어차피
    // SQLite는 쓰기를 내부적으로 직렬화하고, 지금 부하 수준에서 큐가 밀릴
    // 정도로 쿼리가 몰릴 일이 없다.
    jobQueue_.Start(1);

    // 상점/인벤토리 REST API. 게임 포트(9000)와 별개로 8080에서 듣는다.
    httpServer_.Start(8080);
    std::cout << "HTTP shop API is running on port 8080." << std::endl;

    for (int i = 0; i < ioThreadCount_; i++)
    {
        ioThreads_.emplace_back([this]()
                                { IoThreadWorkerLoop(); });
    }

    matchmakingThread_ = std::thread([this]()
                                      { MatchmakingWorkerLoop(); });

    std::cout << "Server is running." << std::endl;

    int id = 0;

    while (running_)
    {
        SOCKET client = accept(listenSocket_, nullptr, nullptr);
        if (client == INVALID_SOCKET)
        {
            // Stop()이 listenSocket_을 닫아서 accept가 깨어난 정상적인 종료 경로.
            // running_이 여전히 true인데 실패했다면 일시적인 오류로 보고 계속 받는다.
            if (!running_)
                break;

            std::cerr << "[Server] accept() 실패: " << WSAGetLastError() << std::endl;
            continue;
        }

        // Nagle 알고리즘을 끈다. 기본값(켜짐)이면 작은 패킷(카드 한 장 정보 등)을
        // 바로 안 보내고 ACK를 기다리며 모아뒀다가 보내서, 부하테스트로 재본 RTT의
        // p99가 p50보다 몇 배씩 튀는 원인이 됐다 (지연 ACK와 겹치면서 수십 ms까지
        // 지연). 이 서버는 게임 패킷 대부분이 작고 빈번해서 처리량보다 지연시간이
        // 중요하다 - 꺼서 즉시 전송되게 한다.
        BOOL noDelay = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char *)&noDelay, sizeof(noDelay));

        CreateIoCompletionPort((HANDLE)client, iocpHandle_, 0, 0);

        Session *session = sessionPool_.Acquire();
        session->Reset();
        session->setId(id);
        session->setNickname(std::to_string(id));
        id++;

        Buffer *recvBuf = bufferPool_.Acquire();
        WSABUF buf;
        buf.buf = recvBuf->data;
        buf.len = 1024;
        CustomOVERLAPPED *recvOverlapped = new CustomOVERLAPPED();
        ZeroMemory(&recvOverlapped->overlapped, sizeof(OVERLAPPED));
        recvOverlapped->socket = client;
        recvOverlapped->buf = buf;
        DWORD flags = 0;
        recvOverlapped->ioType = IOType::RECV;
        recvOverlapped->session = session;
        recvOverlapped->buffer = recvBuf;

        Buffer *sendBuf = bufferPool_.Acquire();
        CustomOVERLAPPED *sendOverlapped = new CustomOVERLAPPED();
        ZeroMemory(&sendOverlapped->overlapped, sizeof(OVERLAPPED));
        sendOverlapped->buf.buf = sendBuf->data;
        sendOverlapped->buf.len = 1024;
        sendOverlapped->ioType = IOType::SEND;
        sendOverlapped->socket = client;
        sendOverlapped->session = session;
        sendOverlapped->buffer = sendBuf;

        session->setSendOverlapped(sendOverlapped);
        session->setRecvOverlapped(recvOverlapped);

        sessionManager_.add(session);

        WSARecv(recvOverlapped->socket, &recvOverlapped->buf, 1, nullptr, &flags, (LPWSAOVERLAPPED)recvOverlapped, NULL);
    }
}

void Server::Stop()
{
    // Ctrl+C 핸들러 스레드와 main 스레드(소멸자)가 동시에 호출할 수 있다.
    // 락을 걸어서, 먼저 들어온 쪽이 정리(스레드 join 포함)를 완전히 끝낼
    // 때까지 나중 호출은 반드시 블로킹되도록 한다 - 그래야 아직 join 안
    // 된 스레드가 있는 상태로 Server 객체가 소멸되는 일이 없다.
    std::lock_guard<std::mutex> lock(stopMutex_);

    if (!running_)
        return; // 이미 다른 스레드가 정리를 끝낸 뒤라 할 일 없음.

    running_ = false;

    // accept()에서 블로킹 중인 메인 스레드를 깨우기 위해 리스닝 소켓을 닫는다.
    closesocket(listenSocket_);

    // GetQueuedCompletionStatus(..., INFINITE)로 블로킹 중인 IO 스레드들을 깨우려면
    // pOverlapped가 nullptr인 더미 completion을 스레드 수만큼 posting해야 한다.
    // IoThreadWorkerLoop는 pOverlapped == nullptr을 종료 신호로 해석한다.
    for (size_t i = 0; i < ioThreads_.size(); i++)
        PostQueuedCompletionStatus(iocpHandle_, 0, 0, nullptr);

    for (auto &t : ioThreads_)
        if (t.joinable())
            t.join();

    if (matchmakingThread_.joinable())
        matchmakingThread_.join();

    httpServer_.Stop();

    // 큐에 남은 DB 작업을 다 처리할 때까지 기다렸다가 닫는다.
    jobQueue_.Stop();
    gameDB_.Close();

    CloseHandle(iocpHandle_);
    WSACleanup();

    std::cout << "Server stopped." << std::endl;
}

void Server::IoThreadWorkerLoop()
{
    while (running_)
    {
        DWORD dwBytesTransferred = 0;
        ULONG_PTR dwCompletionKey = 0;
        OVERLAPPED *pOverlapped = nullptr;
        DWORD flags = 0;

        GetQueuedCompletionStatus(iocpHandle_, &dwBytesTransferred, &dwCompletionKey, &pOverlapped, INFINITE);

        if (pOverlapped == nullptr)
        {
            // Stop()이 스레드를 깨우려고 posting한 더미 completion - 종료 신호.
            break;
        }

        CustomOVERLAPPED *overlapped_ = reinterpret_cast<CustomOVERLAPPED *>(pOverlapped);

        if (dwBytesTransferred == 0 && !overlapped_->session->isReleased_)
        {
            overlapped_->session->isReleased_ = true;

            // 반드시 SessionManager에서 먼저 제거한 뒤에 풀로 돌려줘야 한다.
            // 순서가 바뀌면, 다른 스레드가 accept 루프에서 이 Session 포인터를
            // 새 연결에 재사용(Acquire)하는 사이에 broadcast()가 여전히 이걸
            // 옛 연결로 착각해 Send()를 호출하는 race가 생긴다.
            sessionManager_.remove(overlapped_->session);
            oneCardQueueManager_.Leave(overlapped_->session);
            if (std::shared_ptr<OneCardRoom> room = overlapped_->session->GetOneCardRoom())
                room->HandleDisconnect(overlapped_->session);

            // 연결이 끊긴 소켓 핸들을 실제로 닫는다. 이전까지는 세션/버퍼 풀만
            // 반환하고 소켓 자체는 한 번도 닫지 않아서 연결이 쌓일수록 핸들이 샜다.
            closesocket(overlapped_->socket);

            bufferPool_.Release(overlapped_->session->getRecvOverlapped()->buffer);
            bufferPool_.Release(overlapped_->session->getSendOverlapped()->buffer);
            sessionPool_.Release(overlapped_->session);

            continue;
        }

        if (overlapped_->ioType == IOType::RECV)
        {
            overlapped_->session->OnRecvComplete(dwBytesTransferred);
        }
        else if (overlapped_->ioType == IOType::SEND)
        {
            overlapped_->session->OnSendComplete(dwBytesTransferred);
        }
    }
}

void Server::MatchmakingWorkerLoop()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        oneCardQueueManager_.Tick();
    }
}
