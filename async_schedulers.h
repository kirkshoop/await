#pragma once

#include <windows.h>
#include <threadpoolapiset.h>

namespace async {
    namespace scheduler {
        // usage: await schedule(std::chrono::system_clock::now() + 100ms, [](){. . .});
        template<class Work>
        auto schedule(std::chrono::system_clock::time_point at, Work work) {
            class awaiter {
                static void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, void* Context, PTP_TIMER) {
                    ex::resumable_handle<>::from_address(Context)();
                }
                PTP_TIMER timer = nullptr;
                std::chrono::system_clock::time_point at;
                Work work;
            public:
                awaiter(std::chrono::system_clock::time_point a, Work w) : at(a), work(std::move(w)) {}
                bool await_ready() const {
                    //std::cout << "ready " << std::this_thread::get_id() << std::endl;
                    return std::chrono::system_clock::now() >= at;
                }
                void await_suspend(ex::resumable_handle<> resume_cb) {
                    //std::cout << "suspend " << std::this_thread::get_id() << std::endl;
                    auto duration = at - std::chrono::system_clock::now();
                    int64_t relative_count = -duration.count();
                    timer = CreateThreadpoolTimer(TimerCallback, resume_cb.to_address(), nullptr);
                    if (timer == 0) throw std::system_error(GetLastError(), std::system_category());
                    SetThreadpoolTimer(timer, (PFILETIME)&relative_count, 0, 0);
                }
                auto await_resume() {
                    //std::cout << "resume " << std::this_thread::get_id() << std::endl;
                    return work();
                }
                ~awaiter() {
                    //std::cout << "close " << std::this_thread::get_id() << std::endl;
                    if (timer) CloseThreadpoolTimer(timer);
                }
            };
            return awaiter{ at, work };
        }

        // usage: for await (r : schedule_periodically(std::chrono::system_clock::now(), 100ms, [](int64_t tick){. . .})){. . .}
        template<class Work, typename U = decltype(std::declval<Work>()(0))>
        async::async_generator<U> schedule_periodically(std::chrono::system_clock::time_point initial, std::chrono::system_clock::duration period, Work work) {
            int64_t tick = 0;
            for (;;) {
                auto result = __await schedule(initial + (period * tick), [&tick, &work]() {
                    return work(tick);
                });
                __yield_value result;
                ++tick;
            }
        }

    }
}
