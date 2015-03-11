#pragma once

namespace await_rx {
	namespace rx = rxcpp;

	template <typename T = void>
	struct coro;

	template <>
	struct coro<void>
	{
		~coro() {}

		struct promise_type
		{
			auto get_return_object() { return coro{}; }
			void set_result() {}
			template <typename E> void set_exception(E const&) {}
			auto initial_suspend() { return std::experimental::suspend_never{}; }
			auto final_suspend() { return std::experimental::suspend_never{}; }
			bool cancellation_requested() { return false; }
		};
	};

	template<class ARange, class Promise = ex::resumable_traits<ARange>::promise_type>
	auto make_observable(ARange ar) {
		auto subscribe = [](auto out, auto cold) -> coro<void> {
			try {
				for __await(auto v : cold) {
					out.on_next(v);
					if (!out.is_subscribed()) { break; }
				}
			}
			catch (...) {
				out.on_error(std::current_exception());
				return;
			}
			out.on_completed();
		};
		return rx::observable<>::create<ARange::value_type>(
			[ar, &subscribe](auto out) {
			subscribe(out, ar);
		}
		);
	}

	struct rendevous {
		std::mutex Lock;
		std::condition_variable Wait;
		void notify() {
			Wait.notify_one();
		}
		template<class P>
		void wait(P p) {
			std::unique_lock<std::mutex> guard(Lock);
			Wait.wait(guard, p);
		}
	};

	template<class T>
	async::async_generator<T> make_async_generator(rx::observable<T> s) {
		struct resumer : public std::enable_shared_from_this<resumer> {
			bool Done = false;
			std::exception_ptr Error;
			ex::resumable_handle<> Resume;
			T CurrentValue;
			bool Subscribed = false;
			rx::observable<T> S;
			std::shared_ptr<rendevous> Rendevous;

			~resumer() {
			}
			resumer(rx::observable<T> s) : S(s), Error{ nullptr }, Resume{ nullptr }, Rendevous(std::make_shared<rendevous>()) {}

			bool await_ready() noexcept
			{
				return false;
			}

			void await_suspend(ex::resumable_handle<> r) noexcept
			{
				auto that = this->shared_from_this();
				that->Resume = r;
				that->Rendevous->notify();
				if (!that->Subscribed) {
					that->Subscribed = true;
					auto resumable = [that]() {
						return !!that->Resume;
					};
					S.subscribe(
						[=](T v) { that->Rendevous->wait(resumable); that->CurrentValue = v; that->resume(); },
						[=](std::exception_ptr e) { that->Rendevous->wait(resumable); that->Error = e; that->Done = true; that->resume(); },
						[=]() { that->Rendevous->wait(resumable); that->Done = true; that->resume(); });
				}
			}

			void await_resume() noexcept
			{
			}


			void resume() {
				auto c = Resume;
				Resume = nullptr;
				c();
			}

			T get() {
				if (Error) {
					std::rethrow_exception(Error);
				}
				return CurrentValue;
			}
		};
		auto Resumer = std::make_shared<resumer>(s);
		for (;;) {
			__await *Resumer;
			if (Resumer->Done) { break; }
			__yield_value Resumer->get();
		}
	}

}
