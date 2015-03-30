#pragma once

#include <windows.h>
#include <threadpoolapiset.h>

namespace async { namespace scheduler {


        // usage: await schedule(std::chrono::system_clock::now() + 100ms, [](){. . .});
        template<class Work, class T = std::result_of_t<Work()>>
        auto schedule(std::chrono::system_clock::time_point at, Work work) {
            class awaiter {
                static void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, void* Context, PTP_TIMER) {
                    ex::resumable_handle<>::from_address(Context)();
                }
                PTP_TIMER timer = nullptr;
                std::chrono::system_clock::time_point at;
                Work work;
                ex::resumable_handle<> coro;
                bool canceled = false;
            public:
                awaiter(std::chrono::system_clock::time_point a, Work w) : at(a), work(std::move(w)) {}
                bool await_ready() const {
                    return std::chrono::system_clock::now() >= at;
                }
                void await_suspend(ex::resumable_handle<> resume_cb) {
                    coro = resume_cb;
                    auto duration = at - std::chrono::system_clock::now();
                    int64_t relative_count = -duration.count();
                    timer = CreateThreadpoolTimer(TimerCallback, coro.to_address(), nullptr);
                    if (timer == 0) throw std::system_error(GetLastError(), std::system_category());
                    SetThreadpoolTimer(timer, (PFILETIME)&relative_count, 0, 0);
                }
                auto await_resume() {
                    if (timer) {
                        CloseThreadpoolTimer(timer);
                        timer = nullptr;
                        coro = nullptr;
                    }
                    if (canceled) {throw std::exception("canceled!");}
                    return work();
                }
                void cancel() {
                    if (timer) {
                        std::cout << "cancel schedule" << std::endl;
                        canceled = true;
                        SetThreadpoolTimer(timer, nullptr, 0, 0);
                        WaitForThreadpoolTimerCallbacks(timer, true);
                        coro();
                    }
                }
                ~awaiter() {
                    cancel();
                }
            };

            return awaiter{at, work};
        }

        // usage: for await (r : schedule_periodically(std::chrono::system_clock::now(), 100ms, [](int64_t tick){. . .})){. . .}
        template<class Work, typename U = decltype(std::declval<Work>()(0))>
        async_generator<U> schedule_periodically(std::chrono::system_clock::time_point initial, std::chrono::system_clock::duration period, Work work) {
            int64_t tick = 0;
            record_lifetime scope(__FUNCTION__);
            auto what = [&]{
                scope(" fired!");
                return work(tick);
            };

            for (;;) {
                auto when = initial + (period * tick);
                auto ticker = as::schedule(when, what);

                __await async::attach_oncancel{[&](){
                    scope(" cancel!");
                    ticker.cancel();
                }};

                scope(" await");
                auto result = __await ticker;

                __await async::attach_oncancel{[](){
                }};

                scope(" yield");
                __yield_value result;
                ++tick;
            }
        }

    }
}
