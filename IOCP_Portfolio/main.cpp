#include <iostream>
#include "Server.h" // winsock2.h를 windows.h보다 먼저 끌어오므로, windows.h를 따로 먼저 include하면 안 된다.

Server *g_Server = nullptr;

// Ctrl+C, Ctrl+Break, 콘솔 창 닫기(X 버튼) 등을 잡아서 Server::Stop()을 호출한다.
// 이게 없으면 서버는 강제 종료(프로세스 kill)로만 멈출 수 있어서, 소켓 정리도
// 스레드 join도 전혀 안 된 채로 죽는다.
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    if (g_Server != nullptr)
        g_Server->Stop();

    return TRUE;
}

int main()
{
    // /utf-8로 빌드해서 문자열 리터럴이 UTF-8인데, cmd 콘솔 기본 코드페이지(보통
    // 949)로 그대로 출력하면 한글이 깨진다. 콘솔 출력 코드페이지를 UTF-8로 맞춘다.
    SetConsoleOutputCP(CP_UTF8);

    Server server(2);
    g_Server = &server;

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    server.Start(9000);

    std::cout << "Server exited cleanly." << std::endl;
    return 0;
}
