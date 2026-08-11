#pragma once

#include "Session.h"
#include <map>
#include <iterator>
#include <iostream>
#include <mutex>
#include "PacketHeader.h"

class SessionManager
{
public:
    void add(Session *session);
    void remove(Session *session);
    void broadcast(Session *session, PacketId id, const char *data, DWORD len);

private:
    std::map<int, Session *> sessions_;
    // IO 스레드가 여러 개라 add/remove/broadcast가 서로 다른 스레드에서
    // 동시에 sessions_에 접근할 수 있다. 락 없이는 data race.
    std::mutex mutex_;
};
