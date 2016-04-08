// await.cpp : Defines the entry point for the console application.
//
// In VS 2015 x64 Native prompt:
// CL.exe /Zi /nologo /W3 /sdl- /Od /D _DEBUG /D WIN32 /D _CONSOLE /D _UNICODE /D UNICODE /Gm /EHsc /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TP /await await.cpp
//

#include <iostream>
#include <future>

#include <chrono>
namespace t = std::chrono;
using clk = t::system_clock;
using namespace std::chrono_literals;

#include <experimental/resumable>
namespace ex = std::experimental;

#define co_await __await
#define co_yield __yield_value

#include <windows.h>
#include <threadpoolapiset.h>

// usage: await schedule(std::chrono::system_clock::now() + 1s, [](){. . .});
template<class Work>
auto schedule(clk::time_point at, Work work) {
    class awaiter {
        static void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, void* Context, PTP_TIMER) {
            ex::coroutine_handle<>::from_address(Context)();
        }
        PTP_TIMER timer = nullptr;
        std::chrono::system_clock::time_point at;
        Work work;
    public:
        awaiter(std::chrono::system_clock::time_point a, Work w)
            : at(a), work(std::move(w)) {}
        bool await_ready() const {
            return std::chrono::system_clock::now() >= at;
        }
        void await_suspend(ex::coroutine_handle<> resume_cb) {
            auto duration = at - std::chrono::system_clock::now();
            int64_t relative_count = -duration.count();
            timer = CreateThreadpoolTimer(TimerCallback, resume_cb.to_address(), nullptr);
            if (timer == 0)
                throw std::system_error(GetLastError(), std::system_category());
            SetThreadpoolTimer(timer, (PFILETIME)&relative_count, 0, 0);
        }
        auto await_resume() {
            return work();
        }
        ~awaiter() {
            if (timer) CloseThreadpoolTimer(timer);
        }
    };
    return awaiter{ at, work };
}

template<class... T>
void outln(T... t) {
    std::cout << std::this_thread::get_id();
    int seq[] = {(std::cout << t, 0)...};
    std::cout << std::endl;
}

std::future<int> schedule_test() {
    outln(" - schedule_test ");
    auto answer = co_await schedule(clk::now() + 1s, []() {
        outln(" - schedule_test - lambda");
        return 42;
    });
    outln(" - schedule_test - answer = ", answer);
    return answer;
}

int wmain() {
    try {
        outln(" - main ");
        auto pending = schedule_test();
        outln(" - main - returned");
        int answer = pending.get();
        outln(" - main - answer = ", answer);
    }
    catch (const std::exception& e) {
        outln("schedule_test exception ", e.what());
    }
}
