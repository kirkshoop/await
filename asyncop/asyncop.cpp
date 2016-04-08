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
            record_lifetime scope(__FUNCTION__);
            auto what = [&]{
                scope(" fired!");
                return work(tick);
            };

            for (;;) {
                auto when = initial + (period * tick);
                auto ticker = as::schedule(when, what);

                co_await async::attach_oncancel{[&](){
                    scope(" cancel!");
                    ticker.cancel();
                }};

                scope(" await");
                auto result = co_await ticker;

                co_await async::attach_oncancel{[](){
                }};

                scope(" yield");
                co_yield result;
                ++tick;
            }
        });
}

template<class... T>
void outln(T... t) {
    std::unique_lock<std::mutex> guard(outlock);
    std::cout << std::this_thread::get_id();
    int seq[] = {(std::cout << t, 0)...};
    std::cout << std::endl;
}

auto async_test() -> std::future<void> {
    auto start = clk::now();

    auto ticks = schedule_periodically(start + 1s, 1s, [](int64_t tick) {return tick; }) |
        ao::filter([](int64_t ){return true;}) |
        ao::map([](int64_t v){return v;}) |
        ao::take(5) |
        ao::filter([](int64_t ){return true;}) |
        ao::map([](int64_t v){return v;});

    outln(" async_test await");
    for co_await(auto&& rt : ticks.subscribe() ) {
        outln(" async_test for - ", rt);
        outln(" async_test await");
    }
    outln(" async_test exit");
}

template<class T, class Subscriber>
auto asyncop_test(async::async_observable<T*, Subscriber>& test) -> std::future<void> {
    outln(" asyncop await");
    for co_await(auto&& rt : test.subscribe()) {
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

#if 1
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

#if 0
    try {
        outln(" wmain start 1");
        auto start = clk::now();
        auto test = schedule_periodically(start + 1s, 1s, [](int64_t tick) {return tick; }) |
            ao::filter([](int64_t t) { return (t % 2) == 0; }) |
            ao::flat_map([start](int64_t st) {
                return schedule_periodically(start + (1s * st) + 1s, 1s, [](int64_t tick) {return tick; }) |
                    ao::filter([](int64_t t) { return (t % 2) == 0; }) |
                    ao::map([st](int64_t tick) {
                        auto ss = std::make_unique<std::stringstream>();
                        *ss << std::this_thread::get_id() << " " << tick + (100 * (st + 1));
                        return ss.release();
                    });
            }) |
            ao::map([](std::stringstream* ss) {
                *ss << " " << std::this_thread::get_id();
                return ss;
            }) |
            ao::take(5);
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
        std::this_thread::sleep_for(2s);
    }
    catch (const std::exception& e) {
        outln(" exception ", e.what());
    }
#endif
}
