// asyncop.cpp : Defines the entry point for the console application.
//
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <iostream>
#include <future>
#include <sstream>

#include <chrono>
namespace t = std::chrono;
using clk = t::system_clock;
using namespace std::chrono_literals;

#include <experimental/resumable>
#include <experimental/generator>
namespace ex = std::experimental;

#include "../async_generator.h"
#include "../async_observable.h"
#include "../async_schedulers.h"
namespace as = async::scheduler;
#include "../async_operators.h"
namespace ao = async::operators;

template<class Work, typename U = std::result_of_t<Work(int64_t)>>
auto schedule_periodically(
    clk::time_point initial,
    clk::duration period,
    Work work) {

    return async::async_observable<U>::create(
        [=]() mutable -> async::async_generator<U> {
            int64_t tick = 0;
            auto what = [&]{
                return work(tick);
            };
            bool canceled = false;
            std::function<void()> cancel{nullptr};
            __await async::add_oncancel([&](){
                canceled = true;
                if (cancel) {cancel();}
            });

            while (!canceled) {
                auto when = initial + (period * tick);
                auto next = as::schedule(when, what);

                cancel = [&](){
                    next.cancel();
                };
                auto result = __await next;

                if (canceled) {
                    break;
                }
                cancel = nullptr;

                __yield_value result;
                ++tick;
            }
        });
}

auto outlock = std::make_shared<std::mutex>();
template<class... T>
void outln(T... t) {
    std::unique_lock<std::mutex> guard(*outlock);
    std::cout << std::this_thread::get_id();
    int seq[] = {(std::cout << t, 0)...};
    std::cout << std::endl;
}

auto async_test() -> std::future<void> {
    auto start = clk::now();

    auto ticks = schedule_periodically(start + 1s, 1s, [](int64_t tick) {return tick; }) |
        ao::filter([](int64_t ){return true;}) |
        //ao::map([](int64_t v){return v;}) |
        ao::take(2) |
        //ao::filter([](int64_t ){return true;}) |
        ao::map([](int64_t v){return v;});

    outln(" async_test await");
    for __await(auto&& rt : ticks.subscribe() ) {
        outln(" async_test for - ", rt);
        outln(" async_test await");
    }
    outln(" async_test exit");
}

template<class T, class Subscriber>
auto asyncop_test(async::async_observable<T*, Subscriber>& test) -> std::future<void> {
    outln(" asyncop await");
    for __await(auto&& rt : test.subscribe()) {
        [&]() {
            outln(" asyncop for - ", rt->str());
            delete rt;
        }();
        outln(" asyncop await");
    }
    outln(" asyncop test");
}

int wmain() {
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

#if 0
    try {
        outln(" wmain start");
        auto done = []() -> std::future<void> {
            auto start = clk::now();
            auto work = []() {outln(" work!"); return 42; };

#if 0
            {
                outln(" test await..");
                auto a1 = as::await_threadpooltimer<decltype(work)>{start + 1s, work};
                auto cancela1 = std::async([&](){
                    std::this_thread::sleep_until(start + 500ms);
                    a1.foo();
                    //a1.cancel();
                });
                auto value = __await a1;
                outln(" test resume ");
                cancela1.get();
                __await a1.complete();
                outln(" test finished ");
            }
#endif
#if 0
            {
                auto s = as::schedule(start + 1s, work);

                outln(" test await..");
#if 0
                auto cancels = std::async([&](){
                    std::this_thread::sleep_until(start + 500ms);
                    s.cancel();
                });
#endif
                auto r = __await s.run();
                outln(" test resume.. empty ", std::boolalpha, r.empty() );
            }
#endif
#if 0
            auto tick = as::schedule(start + 1s, work);

            {
            outln(" test await..");
            auto a1 = __await tick.run();
            outln(" test resume.. ", a1.get());
            }

            std::this_thread::sleep_for(2s);

            {
            outln(" test await..");
            auto a2 = __await tick.run();
            outln(" test resume.. ", a2.get());
            }
#endif

        }();
        outln(" wmain wait..");
        done.get();
        outln(" wmain done");
        std::this_thread::sleep_for(4s);
    }
    catch (const std::exception& e) {
        outln(" exception ", e.what());
    }
#endif

#if 0
    try {
        outln(" wmain start");
        auto done = async_test();
        outln(" wmain wait..");
        done.get();
        outln(" wmain done");
        std::this_thread::sleep_for(2s);
    }
    catch (const std::exception& e) {
        outln(" exception ", e.what());
    }
#endif

#if 1
    try {
        outln(" wmain start 1");
        auto start = clk::now();
        int st = 0;
        auto test = ((((schedule_periodically(start + 1s, 1s, [](int64_t tick) {return tick; }).set_id("ticks") |
            ao::filter([](int64_t t) { return (t % 2) == 0; })).set_id("even ticks") |
//            ao::flat_map([start](int64_t st) {
//                return schedule_periodically(start + (1s * st) + 1s, 1s, [](int64_t tick) {return tick; }) |
//                    ao::filter([](int64_t t) { return (t % 2) == 0; }) |
                    ao::map([st](int64_t tick) {
                        auto ss = std::make_unique<std::stringstream>();
                        *ss << std::this_thread::get_id() << " " << tick + (100 * (st + 1));
                        return ss.release();
//                    });
            })).set_id("tick as string") |
            ao::map([](std::stringstream* ss) {
                *ss << " " << std::this_thread::get_id();
                return ss;
            })).set_id("add thread id") |
            ao::take_until(schedule_periodically(start + 5s, 5s, [](int64_t tick) {return tick; }).set_id("cancelation"))).set_id("take 5s");
        auto done1 = asyncop_test(test);
        outln(" wmain wait 1 ..");
        done1.get();
#if 0
        std::this_thread::sleep_for(2s);
        outln(" wmain start 2");
        auto done2 = asyncop_test(test);
        outln(" wmain wait 2 ..");
        done2.get();
#endif
        outln(" wmain done");
        std::this_thread::sleep_for(10s);
    }
    catch (const std::exception& e) {
        outln(" exception ", e.what());
    }
#endif
}
