// Minimal PPL task compatibility used when cross-compiling VSTGUI with
// MinGW-w64. VSTGUI only needs task<void>, construction from a callable,
// and continuation chaining from Microsoft's ppltasks.h.

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace concurrency {

template <typename Result>
class task;

template <>
class task<void> {
public:
    task() = default;

    template <typename Function>
    explicit task(Function&& function)
        : state_(std::make_shared<State>())
    {
        auto state = state_;
        std::thread([state,
                        function = std::forward<Function>(function)]() mutable {
            try {
                function();
            } catch (...) {
                // VSTGUI's task callbacks are fire-and-forget. Match that
                // boundary without allowing an exception to terminate REAPER.
            }
            state->complete();
        }).detach();
    }

    template <typename Function>
    task<void> then(Function&& function) const
    {
        auto previous = state_;
        return task<void>([previous,
                              function = std::forward<Function>(function)]() mutable {
            if (previous) previous->wait();
            function();
        });
    }

private:
    struct State {
        void complete()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                finished = true;
            }
            condition.notify_all();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this] { return finished; });
        }

        std::mutex mutex;
        std::condition_variable condition;
        bool finished { false };
    };

    std::shared_ptr<State> state_;
};

} // namespace concurrency
