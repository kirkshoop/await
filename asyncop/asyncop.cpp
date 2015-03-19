// asyncop.cpp : Defines the entry point for the console application.
//

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
#include "../async_schedulers.h"
namespace as = async::scheduler;
#include "../async_operators.h"
namespace ao = async::operators;

template<class Work, typename U = decltype(std::declval<Work>()(0))>
async::async_generator<U> schedule_periodically(
    clk::time_point initial,
    clk::duration period,
    Work work) {
    int64_t tick = 0;
    auto what = [&tick, &work]() {
        return work(tick);
    };
    for (;;) {
        auto when = initial + (period * tick);
        auto result = __await as::schedule(when, what);
        __yield_value result;
        ++tick;
    }
}

template<class... T>
void outln(T... t) {
    std::cout << std::this_thread::get_id();
    int seq[] = {(std::cout << t, 0)...};
    std::cout << std::endl;
}

std::future<void> async_test() {
    auto start = clk::now();
    for __await(auto&& rt :
        schedule_periodically(start + 1s, 1s,
            [](int64_t tick) {return tick; }) ) {
        outln(" for await - ", rt);
        if (rt == 5) break;
    }
}

std::future<void> asyncop_test() {
    auto start = clk::now();
    for __await(auto&& rt :
        as::schedule_periodically(start + 1s, 1s, [](int64_t tick) {return tick; }) |
        ao::filter([](int64_t t) { return (t % 2) == 0; }) |
        ao::flat_map([start](int64_t st) {
            return as::schedule_periodically(start + (1s * st) + 1s, 1s, [](int64_t tick) {return tick; }) |
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
        ao::take(10)) {

        [&]() {
            outln(" for await - ", rt->str());
            delete rt;
        }();
    }
    outln(" asyncop test");
}

int wmain() {
    try {
        outln(" wmain start");
        auto done = async_test();
        outln(" wmain wait..");
        done.get();
        outln(" wmain done");
    }
    catch (const std::exception& e) {
        outln(" exception ", e.what());
    }
    try {
        outln(" wmain start");
        auto done = asyncop_test();
        outln(" wmain wait..");
        done.get();
        outln(" wmain done");
    }
    catch (const std::exception& e) {
        outln(" exception ", e.what());
    }
}
