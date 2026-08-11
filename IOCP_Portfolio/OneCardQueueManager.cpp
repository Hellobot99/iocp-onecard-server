#include "OneCardQueueManager.h"
#include "OneCardRoom.h"
#include "Session.h"
#include <algorithm>

void OneCardQueueManager::Join(Session *session)
{
    std::lock_guard<std::mutex> lock(mutex_);

    for (Session *s : waiting_)
        if (s == session)
            return; // 이미 대기 중

    if (waiting_.empty())
        firstJoinedAt_ = std::chrono::steady_clock::now();

    waiting_.push_back(session);
}

void OneCardQueueManager::Leave(Session *session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    waiting_.erase(std::remove(waiting_.begin(), waiting_.end(), session), waiting_.end());
}

void OneCardQueueManager::Tick()
{
    std::vector<Session *> toStart;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (waiting_.empty())
            return;

        bool full = static_cast<int>(waiting_.size()) >= kMaxPlayers;
        bool timedOut = static_cast<int>(waiting_.size()) >= kMinPlayers &&
                        (std::chrono::steady_clock::now() - firstJoinedAt_) >= kQueueTimeout;

        if (!full && !timedOut)
            return;

        size_t count = std::min(waiting_.size(), static_cast<size_t>(kMaxPlayers));
        toStart.assign(waiting_.begin(), waiting_.begin() + count);
        waiting_.erase(waiting_.begin(), waiting_.begin() + count);

        if (!waiting_.empty())
            firstJoinedAt_ = std::chrono::steady_clock::now();
    }

    // 방 생성/게임 시작은 대기열 락을 놓은 뒤에 한다 - StartRoom 안에서
    // 세션에 패킷을 보내는 동안 다른 스레드의 Join/Leave가 막히지 않게.
    StartRoom(std::move(toStart));
}

void OneCardQueueManager::StartRoom(std::vector<Session *> players)
{
    auto room = std::make_shared<OneCardRoom>(players);

    for (size_t i = 0; i < players.size(); ++i)
        players[i]->SetOneCardRoom(room, static_cast<int>(i));

    room->Start();
}
