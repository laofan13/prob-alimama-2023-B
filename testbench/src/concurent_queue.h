#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

#define BOOST_LOG_DYN_LINK 1
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
namespace logging = boost::log;

enum class QueueStatus {
  Closed,
  Ok,
  Empty
};

template<typename T>
class ConcurrentQueue {
public:
    ConcurrentQueue(int32_t capacity=0, std::string tag="")
        : capacity_{capacity}, closed_{false},tag_{tag} {
    }

    QueueStatus Push(const T& v) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_push_.wait(lock, [this] {
            if (this->capacity_ == 0) {
                return true;
            } else {
                return (this->q_.size() < this->capacity_) || this->closed_.load();
            }
        });
        if (closed_) return QueueStatus::Closed;
        q_.push(v);
        lock.unlock();
        cv_pop_.notify_one();
        return QueueStatus::Ok;
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return q_.empty();
    }

    QueueStatus TryPop(T& popped_value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (q_.empty()) return QueueStatus::Empty;
        if (closed_) return QueueStatus::Closed;
        popped_value = q_.front();
        q_.pop();
        cv_push_.notify_one();
        return QueueStatus::Ok;
    }

    QueueStatus WaitAndPop(T& popped_value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_pop_.wait(lock, [this] { return !this->q_.empty() || this->closed_.load(); });
        if (closed_) return QueueStatus::Closed;
        popped_value = q_.front();
        q_.pop();
        BOOST_LOG_TRIVIAL(trace) << tag_ << " WaitAndPop " << q_.size();
        cv_push_.notify_one();
        return QueueStatus::Ok;
    }

    void Close() {
        BOOST_LOG_TRIVIAL(trace) << tag_ << " close queue";
        closed_.store(true);
        cv_push_.notify_all();
        cv_pop_.notify_all();
    }

private:
    std::queue<T> q_;
    mutable std::mutex mutex_;
    std::condition_variable cv_push_;
    std::condition_variable cv_pop_;
    int32_t capacity_;
    std::atomic<bool> closed_;
    std::string tag_;
};
