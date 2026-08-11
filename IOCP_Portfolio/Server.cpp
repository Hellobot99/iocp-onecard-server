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

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    bind(sock, (sockaddr *)&addr, sizeof(addr));
    listen(sock, SOMAXCONN);

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
        SOCKET client = accept(sock, nullptr, nullptr);
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
    running_ = false;
    std::cout << "Server stopped." << std::endl;
}

void Server::IoThreadWorkerLoop()
{
    while (running_)
    {
        DWORD dwBytesTransferred = 0;
        ULONG_PTR dwCompletionKey = 0;
        OVERLAPPED *pOverlapped = nullptr;
        CustomOVERLAPPED *overlapped_ = nullptr;
        DWORD flags = 0;

        if (GetQueuedCompletionStatus(iocpHandle_, &dwBytesTransferred, &dwCompletionKey, &pOverlapped, INFINITE))
        {
            overlapped_ = reinterpret_cast<CustomOVERLAPPED *>(pOverlapped);

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
}

void Server::MatchmakingWorkerLoop()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        oneCardQueueManager_.Tick();
    }
}