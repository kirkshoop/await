#pragma once


template<typename T>
class merge_channel
{
	T* value = nullptr;
	ex::resumable_handle<> coro;

	bool await_ready() const {
		return !!value;
	}
	void await_suspend(ex::resumable_handle<> c) {
		coro = c;
	}
	T& await_resume() {
		return *value;
	}

	void push(T& v) {
		value = std::addressof(v);
		coro();
		value = nullptr;
	}
};

template<typename T>
async_generator<T> merge(async_generator<T> lhs, async_generator<T> rhs) {
	merge_channel<T> pop;
	std::mutex lock;

	auto source = [&](async_generator<T> s) -> std::future<void> {
		for __await(auto& v: s) {
			std::unique_lock<std::mutex> guard(lock);
			pop.push(v);
		}
	};

	auto lf = source(lhs);
	auto rf = source(rhs);

	while(!lf.is_ready() || !rf.is_ready()) {
		auto v = __await pop;
		__yield_value v;
	}
}

