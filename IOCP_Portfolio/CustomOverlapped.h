#pragma once

// windows.h가 min/max를 매크로로 정의해서 std::min/std::max 호출과 충돌한다.
// NOMINMAX로 그 매크로 정의를 막는다.
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include "ObjectPool.h"

#pragma comment(lib, "ws2_32.lib")

class Session;

enum class IOType
{
    RECV,
    SEND
};

struct CustomOVERLAPPED
{
    OVERLAPPED overlapped;
    SOCKET socket;
    WSABUF buf;
    Buffer *buffer;
    IOType ioType;
    Session *session;
};