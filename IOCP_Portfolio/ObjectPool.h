#pragma once

#include <stack>
#include <mutex>

struct Buffer
{
    char data[1024];
};

template <typename T>
class ObjectPool
{
public:
    T *Acquire();
    void Release(T *obj);

private:
    std::stack<T *> stack_;
    std::mutex mutex_;
};

template <typename T>
T *ObjectPool<T>::Acquire()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stack_.empty())
    {
        T *obj = stack_.top();
        stack_.pop();
        return obj;
    }
    return new T();
}

template <typename T>
void ObjectPool<T>::Release(T *obj)
{
    std::lock_guard<std::mutex> lock(mutex_);
    stack_.push(obj);
}
