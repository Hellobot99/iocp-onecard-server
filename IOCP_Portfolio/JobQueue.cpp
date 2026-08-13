#include "JobQueue.h"

void JobQueue::Push(Job job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push(std::move(job));
    }
    cv_.notify_one();
}

void JobQueue::Start(int workerThreadCount)
{
    running_ = true;
    for (int i = 0; i < workerThreadCount; i++)
        workers_.emplace_back([this]()
                               { WorkerLoop(); });
}

void JobQueue::Stop()
{
    running_ = false;
    cv_.notify_all();

    for (auto &t : workers_)
        if (t.joinable())
            t.join();

    workers_.clear();
}

void JobQueue::WorkerLoop()
{
    while (true)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]()
                     { return !jobs_.empty() || !running_; });

            if (jobs_.empty())
            {
                if (!running_)
                    return; // 종료 요청이고 남은 작업도 없으면 끝낸다.
                continue;
            }

            job = std::move(jobs_.front());
            jobs_.pop();
        }

        // 락을 놓은 뒤에 실행한다 - DB 쿼리처럼 오래 걸려도 다른 스레드의
        // Push()나 다른 워커의 작업 처리가 막히지 않는다.
        job();
    }
}
