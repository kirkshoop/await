// rx.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <iostream>
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

#include "rxcpp/rx.hpp"
namespace rx = rxcpp;
namespace rxu = rxcpp::util;

#include "../await_rx.h"
namespace ar = await_rx;

int wmain() {

    auto cold_ticks = []() {
        return ar::make_observable(as::schedule_periodically(t::system_clock::now(), t::seconds(1),
            [](int64_t tick) {
                return tick;
            }));
    };

    struct tick_t { std::string message; int64_t tick; };

    auto cold_odd_ticks = rx::observable<>::defer([&]() {
        return cold_ticks().
            filter([](int64_t t) {return t % 2 != 0; }).
            map([](int64_t t) {
                std::stringstream m;
                m << "cold " << std::this_thread::get_id() << " - tick = " << t;
                return tick_t{ m.str(), t };
            });
    });

    auto hot_even_ticks = cold_ticks().
        filter([](int64_t t) {return t % 2 == 0; }).
        map([](int64_t t) {
            std::stringstream m;
            m << "hot " << std::this_thread::get_id() << " - tick = " << t;
            return tick_t{ m.str(), t };
        }).
        publish().
        connect_forever();

    auto merged = cold_odd_ticks.take(3).
        merge(rx::observe_on_new_thread(), hot_even_ticks.take(3)).
        concat(cold_odd_ticks.take(3).
        merge(rx::observe_on_new_thread(), hot_even_ticks.take(3)));

    merged.
        as_dynamic().
        as_blocking().
        subscribe([&](tick_t t) {
                std::cout << "on_next " << std::this_thread::get_id() << " - t = " << t.tick << " from " << t.message << std::endl;
            },
            [](std::exception_ptr e) {
                try {
                    std::rethrow_exception(e);
                }
                catch (const std::exception& e) {
                    std::cout << "on_error " << std::this_thread::get_id() << " " << e.what() << std::endl;
                }
            });

    std::cout << "tested make_observable " << std::this_thread::get_id() << std::endl;

    try {
        [&]() ->std::future<void> {
            for __await(auto t : ar::make_async_generator(merged.map([](tick_t t) {return new tick_t{ t }; }).as_dynamic())) {
                std::cout << "for await " << std::this_thread::get_id()
                    << " - t = " << t->tick << " from " << t->message
                    << std::endl;
                delete t;
            }
        }().get();
    }
    catch (const std::exception& e) {
        std::cout << "make_async_generator exception " << std::this_thread::get_id() << " " << e.what() << std::endl;
    }

    std::cout << "tested make_async_generator " << std::this_thread::get_id() << std::endl;
}
