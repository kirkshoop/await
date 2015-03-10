#pragma once

namespace async { namespace operators {

	template<typename T, typename P>
	async_generator<T> filter(async_generator<T> s, P p) {
		for __await(auto& v : s) {
			if (p(v)) {
				__yield_value v;
			}
		}
	}

	template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))>
	async_generator<U> map(async_generator<T> s, M m) {
		for __await(auto& v : s) {
			__yield_value m(v);
		}
	}

	template<typename T>
	struct merge_channel
	{
		std::queue<T> queue;
		mutable std::mutex lock;
		ex::resumable_handle<> coro;

		merge_channel() : coro(nullptr) {}

		struct awaitable {
			awaitable(merge_channel* t) : that(t) {}

			bool await_ready() const {
				std::unique_lock<std::mutex> guard(that->lock);
				//std::cout << "merge_channel await_ready " << std::this_thread::get_id() << std::endl;
				return !that->queue.empty();
			}
			void await_suspend(ex::resumable_handle<> c) {
				//std::cout << "merge_channel await_suspend " << std::this_thread::get_id() << std::endl;
				that->coro = c;
			}
			T await_resume() {
				std::unique_lock<std::mutex> guard(that->lock);
				//std::cout << "merge_channel await_resume " << std::this_thread::get_id() << std::endl;
				auto v = that->queue.front();
				that->queue.pop();
				guard.unlock();
				return v;
			}
			merge_channel* that = nullptr;
		};

		awaitable pop(){
			return {this};
		}

		void push(const T& v) {
			std::unique_lock<std::mutex> guard(lock);
			//std::cout << "merge_channel push " << std::this_thread::get_id() << std::endl;
			queue.push(v);
			guard.unlock();
			if(coro){
				ex::resumable_handle<> c = coro;
				coro = nullptr;
				//std::cout << "merge_channel coro " << std::this_thread::get_id() << std::endl;
				c();
				//std::cout << "merge_channel coro-ed " << std::this_thread::get_id() << std::endl;
			}
		}
	};

	template<typename T>
	async_generator<T> merge(async_generator<T> lhs, async_generator<T> rhs) {
		auto ch = std::make_unique<merge_channel<T>>();
		int pending = 2;

		auto source = [&](async_generator<T> s) -> std::future<void> {
			//std::cout << "merge source " << std::this_thread::get_id() << std::endl;
			for __await(auto& v: s) {
				ch->push(v);
			}
			//std::cout << "merge sourced " << std::this_thread::get_id() << std::endl;
			--pending;
		};

		//std::cout << "merge start " << std::this_thread::get_id() << std::endl;
		auto lf = source(std::move(lhs));
		//std::cout << "merge source left " << std::this_thread::get_id() << std::endl;
		auto rf = source(std::move(rhs));
		//std::cout << "merge source right " << std::this_thread::get_id() << std::endl;

		//std::cout << "merge pump " << std::this_thread::get_id() << std::endl;
		while(pending > 0) {
			//std::cout << "merge pump iteration " << std::this_thread::get_id() << std::endl;
			auto v = __await ch->pop();
			__yield_value v;
		}
		//std::cout << "merge pumped " << std::this_thread::get_id() << std::endl;
		lf.get();
		rf.get();
	}

} }
