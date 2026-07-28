
#ifndef __PL_NOTIFY__H__
#define __PL_NOTIFY__H__
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

class ConditionNotifier {
   public:
    ConditionNotifier() = default;

    void notify() {
        std::lock_guard<std::mutex> lk(mtx_);
        ++counter_;
        cv_.notify_one();
    }

    void wait() {  // 1. 永远等
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this] { return counter_ > 0; });
        --counter_;
    }

    template <typename Rep, typename Period>
    bool wait_for(const std::chrono::duration<Rep, Period>& d) {  // 2. 等超时
        std::unique_lock<std::mutex> lk(mtx_);
        if (cv_.wait_for(lk, d, [this] { return counter_ > 0; })) {
            --counter_;
            return true;
        }
        return false;
    }

    template <typename Clock, typename Dur>
    bool wait_until(const std::chrono::time_point<Clock, Dur>& t) {  // 3. 等到时点
        std::unique_lock<std::mutex> lk(mtx_);
        if (cv_.wait_until(lk, t, [this] { return counter_ > 0; })) {
            --counter_;
            return true;
        }
        return false;
    }

    std::size_t pending() const noexcept { return counter_.load(std::memory_order_relaxed); }

   private:
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<std::size_t> counter_{0};
};

#endif
