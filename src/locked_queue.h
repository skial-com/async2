#ifndef ASYNC2_LOCKED_QUEUE_H
#define ASYNC2_LOCKED_QUEUE_H

#include <deque>
#include <mutex>
#include <atomic>

template <class T>
class LockedQueue {
    std::mutex mutex_;
    std::deque<T> queue_;
    std::atomic<bool> has_items_{false};

public:
    void Lock() {
        mutex_.lock();
    }

    void Unlock() {
        mutex_.unlock();
    }

    T Pop() {
        T output = queue_.front();
        queue_.pop_front();
        has_items_.store(!queue_.empty(), std::memory_order_release);
        return output;
    }

    void Push(T item) {
        queue_.push_back(item);
        has_items_.store(true, std::memory_order_release);
    }

    bool Empty() {
        return queue_.empty();
    }

    bool HasItems() const {
        return has_items_.load(std::memory_order_acquire);
    }
};

#endif
