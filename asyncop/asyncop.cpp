// asyncop.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <iostream>
#include <future>
#include <thread>
#include <sstream>
#include <queue>

#include <chrono>
namespace t = std::chrono;
using namespace std::chrono_literals;

#include <experimental/resumable>
#include <experimental/generator>
namespace ex = std::experimental;

#include "../async_generator.h"
#include "../async_schedulers.h"
namespace as = async::scheduler;
#include "../async_operators.h"
namespace ao = async::operators;

std::future<void> async_test() {
	auto start = t::system_clock::now();
	for __await(auto rt :
		as::schedule_periodically(start + 1s, 1s, [](int64_t tick) {return tick; }) |
		ao::filter([](int64_t t) { return (t % 2) == 0; }) |
		ao::flat_map([start](int64_t st) {
			return as::schedule_periodically(start + (1s * st) + 1s, 1s,
				[st](int64_t tick) {
					auto ss = std::make_shared<std::stringstream>();
					*ss << std::this_thread::get_id() << " " << tick + (100 * (st + 1));
					return ss;
				});
		}) |
		ao::map([](std::shared_ptr<std::stringstream> ss) {
			*ss << " " << std::this_thread::get_id();
			return ss;
		}) |
		ao::take(20)) {

		[&]() {
			std::cout << "for " << std::this_thread::get_id()
				<< " - " << rt->str()
				<< std::endl;
		}();
	}
	std::cout << "async test " << std::this_thread::get_id() << std::endl;
}

int wmain() {

	try {
		async_test().get();
	}
	catch (const std::exception& e) {
		std::cout << "exception " << std::this_thread::get_id() << " " << e.what() << std::endl;
	}
}
