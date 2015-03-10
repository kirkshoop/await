#pragma once

#include <iostream>
#include <future>
#include <thread>
#include <sstream>

#include <chrono>
namespace t = std::chrono;

#include <experimental/resumable>
#include <experimental/generator>
namespace ex = std::experimental;

#include <windows.h>
#include <threadpoolapiset.h>

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


namespace inner {
	template<typename T>
	struct yield_to
	{
		yield_to(ex::resumable_handle<>* to, ex::resumable_handle<>* from, T const *& value) : To(to), From(from), CurrentValue(value) {}

		bool await_ready() noexcept
		{
			std::cout << "yield_to await_ready " << std::this_thread::get_id() << std::endl;
			return false;
		}

		void await_suspend(ex::resumable_handle<> r) noexcept
		{
			std::cout << "yield_to await_suspend " << std::this_thread::get_id() << " " << (CurrentValue ? *CurrentValue : 0) << std::endl;
			*To = r;
			if (*From) {
				ex::resumable_handle<> coro{ *From };
				*From = nullptr;

				std::cout << "yield_to continue from" << std::endl;
				coro();
				std::cout << "yield_to continued from" << std::endl;
			}
		}

		void await_resume() noexcept
		{
			std::cout << "yield_to await_resume " << std::this_thread::get_id() << std::endl;
		}

		ex::resumable_handle<>* To;
		ex::resumable_handle<>* From;
		T const *& CurrentValue = nullptr;
	};

	template<typename T>
	struct yield_from
	{
		yield_from(ex::resumable_handle<>* to, ex::resumable_handle<>* from, T const *& value) : wait(to, from, value) {}

		auto& yield_wait() {
			return wait;
		}

		bool await_ready() noexcept
		{
			std::cout << "yield_from await_ready " << std::this_thread::get_id() << std::endl;
			return false;
		}

		void await_suspend(ex::resumable_handle<> r) noexcept
		{
			std::cout << "yield_from await_suspend " << std::this_thread::get_id() << " " << (wait.CurrentValue ? *wait.CurrentValue : 0) << std::endl;
			*wait.From = r;
			if (*wait.To) {
				ex::resumable_handle<> coro{ *wait.To };
				*wait.To = nullptr;
				std::cout << "yield_from continue to" << std::endl;
				coro();
				std::cout << "yield_from continued to" << std::endl;
			}
		}

		void await_resume() noexcept
		{
			std::cout << "yield_from await_resume " << std::this_thread::get_id() << std::endl;
		}

		yield_to<T> wait;
	};

	template<typename T, typename Alloc = std::allocator<char>>
	struct runner
	{
		struct promise_type {
			T const * CurrentValue = nullptr;
			bool done = false;
			std::exception_ptr Error;
			ex::resumable_handle<> To{ nullptr };
			ex::resumable_handle<> From{ nullptr };
			yield_from<T> resumer;

			~promise_type() {
				std::cout << "runner promise destroy" << std::endl;
			}
			promise_type() : resumer(std::addressof(To), std::addressof(From), CurrentValue) {
				std::cout << "runner promise default" << std::endl;
			}

			promise_type& get_return_object()
			{
				std::cout << "runner promise return" << std::endl;
				return *this;
			}

			ex::suspend_always initial_suspend()
			{
				std::cout << "runner promise initial" << std::endl;
				return{};
			}

			ex::suspend_always final_suspend()
			{
				std::cout << "runner promise final" << std::endl;
				if (To) {
					std::cout << "runner promise final to" << std::endl;
					To();
					std::cout << "runner promise final post to" << std::endl;
				}
				return{};
			}

			bool cancellation_requested() const
			{
				std::cout << "runner promise cancelled?" << std::endl;
				return false;
			}

			void set_result()
			{
				std::cout << "runner promise result" << std::endl;
				done = true;
			}

			void set_exception(std::exception_ptr Exc)
			{
				std::cout << "runner promise exception" << std::endl;
				Error = std::move(Exc);
				done = true;
			}

			yield_from<T> yield_value(T const& Value)
			{
				std::cout << "runner promise yield " << Value << std::endl;
				CurrentValue = std::addressof(Value);
				return resumer;
			}
		};

		explicit runner(promise_type& Prom)
			: Coro(ex::resumable_handle<promise_type>::from_promise(_STD addressof(Prom)))
		{
			std::cout << "runner from promise" << std::endl;
		}

		~runner() {
			std::cout << "runner destroy" << std::endl;
		}
		runner() {
			std::cout << "runner default" << std::endl;
		}

		runner(runner const&) = delete;

		runner& operator = (runner const&) = delete;

		runner(runner && Right)
			: Coro(Right.Coro)
		{
			std::cout << "runner copy" << std::endl;
			Right.Coro = nullptr;
		}

		runner& operator = (runner && Right)
		{
			std::cout << "runner assign" << std::endl;

			if (&Right != this)
			{
				Coro = Right.Coro;
				Right.Coro = nullptr;
			}
		}

		std::future<void> go() {
			std::cout << "runner coro " << std::this_thread::get_id() << std::endl;
			Coro();

			std::cout << "runner coro-ed " << std::this_thread::get_id() << std::endl;

			for (;;) {
				__await yield_wait();

				if (Coro.promise().Error)
					std::rethrow_exception(Coro.promise().Error);

				if (Coro.promise().done) { break; }

				std::cout << "runner resumed " << std::this_thread::get_id() << " " << *(Coro.promise().CurrentValue) << std::endl;

				//if (*(Coro.promise().CurrentValue) > 2) { break; }
				//if (*(Coro.promise().CurrentValue) > 2) { throw std::exception("go exception"); }
			}
		}

		yield_to<T> yield_wait() {
			return Coro.promise().resumer.wait;
		}

	private:
		ex::resumable_handle<promise_type> Coro = nullptr;
	};
}

namespace std {
	namespace experimental {
		template <typename T, typename Alloc, typename... Whatever>
		struct resumable_traits<::inner::runner<T, Alloc>, Whatever...> {
			using allocator_type = Alloc;
			using promise_type = typename ::inner::runner<T, Alloc>::promise_type;
		};
	}
}

// usage: for await (r : schedule_periodically(std::chrono::system_clock::now(), 100ms, [](int64_t tick){. . .})){. . .}
template<class Work>
inner::runner<int64_t> inner_schedule_periodically_for(std::chrono::system_clock::time_point initial, std::chrono::system_clock::duration period, Work work) {
	int64_t tick = 0;
	for (;;) {
		if (tick > 4) { break; }
		//		if (tick > 4) { throw std::exception("exit");}
		std::cout << "schedule " << tick << std::endl;
		auto result = __await schedule(initial + (period * tick), [&tick, &work]() {
			std::cout << "work     " << tick << std::endl;
			return work(tick);
		});
		std::cout << "yeild    " << tick << std::endl;
		__yield_value result;
		std::cout << "yeilded  " << tick << std::endl;
		++tick;
	}
}

int wmain() {
	try {
		inner_schedule_periodically_for(t::system_clock::now() + t::seconds(1), t::seconds(1), [](int64_t tick) {
			std::cout << "lambda " << std::this_thread::get_id() << " - tick = " << tick << std::endl;
			return tick;
		}).go().get();
	}
	catch (...) {
		std::cout << "exception " << std::this_thread::get_id() << std::endl;
	}
}