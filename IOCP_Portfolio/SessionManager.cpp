#include "SessionManager.h"

void SessionManager::add(Session *session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_[session->getid()] = session;
}

void SessionManager::remove(Session *session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session->getid());
}

void SessionManager::broadcast(Session *session, PacketId id, const char *data, DWORD len)
{
    // 브로드캐스트가 끝날 때까지 락을 쥐고 있어야, 그 사이에 remove()로
    // 세션이 지워지고 풀로 반환되어 재사용되는 것을 막을 수 있다.
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &pair : sessions_)
    {
        pair.second->Send(id, data, len);
    }
}
