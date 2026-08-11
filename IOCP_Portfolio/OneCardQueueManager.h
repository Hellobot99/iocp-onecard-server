#pragma once

#include <vector>
#include <mutex>
#include <chrono>

class Session;

// 원카드 매칭 대기열.
// - 4명이 모이면 바로 방을 만든다.
// - 4명이 안 모여도 일정 시간(kQueueTimeout)이 지나면 그때까지 모인 인원
//   그대로 시작한다 (1명이어도 시작 - 테스트 편의를 위한 값. 실제 서비스라면
//   kMinPlayers를 2 이상으로 올려야 한다).
class OneCardQueueManager
{
public:
    static constexpr int kMinPlayers = 1;
    static constexpr int kMaxPlayers = 4;
    static constexpr std::chrono::seconds kQueueTimeout{15};

    void Join(Session *session);
    void Leave(Session *session);

    // Server의 매칭 타이머 스레드가 주기적으로(예: 1초마다) 호출.
    void Tick();

private:
    void StartRoom(std::vector<Session *> players);

    std::mutex mutex_;
    std::vector<Session *> waiting_;
    std::chrono::steady_clock::time_point firstJoinedAt_;
};
