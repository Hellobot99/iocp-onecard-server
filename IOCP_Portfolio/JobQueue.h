#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <vector>
#include <atomic>

// 스레드-세이프 작업 큐. DB 쿼리처럼 블로킹될 수 있는 작업을 IOCP 워커 스레드가
// 직접 하지 않고 여기 Push만 해두면, 전용 워커 스레드가 꺼내서 처리한다.
// IOCP 스레드는 Push만 하고 바로 다음 패킷 처리로 돌아갈 수 있어서, DB가 느려져도
// 다른 클라이언트의 패킷 처리가 밀리지 않는다.
class JobQueue
{
public:
    using Job = std::function<void()>;

    void Push(Job job);

    void Start(int workerThreadCount);

    // 큐에 남은 작업을 전부 처리할 때까지 기다렸다가 워커 스레드를 종료한다.
    void Stop();

private:
    void WorkerLoop();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};
};
