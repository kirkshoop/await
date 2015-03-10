#pragma once

namespace async {
	namespace ex = std::experimental;

	template<typename T, typename Promise>
	struct yield_to;

	template<typename T, typename Promise>
	struct async_iterator : public std::iterator<std::input_iterator_tag, T>
	{
		async_iterator(Promise* p) : p(p) {
			//std::cout << "async_iterator construct " << std::this_thread::get_id() << " " << (size_t)p << std::endl;
		}

		yield_to<T, Promise> operator++()
		{
			return{ p };
		}

		async_iterator operator++(int) = delete;
		// generator iterator current_value
		// is a reference to a temporary on the coroutine frame
		// implementing postincrement will require storing a copy
		// of the value in the iterator.
		//{
		//	auto _Result = *this;
		//	++(*this);
		//	return _Result;
		//}

		bool operator==(async_iterator const& Right) const
		{
			//std::cout << "async_iterator == " << std::this_thread::get_id() << " " << (size_t)p << " ?? " << (size_t)Right.p << std::endl;
			return !!p ? p->done || p == Right.p : p == Right.p || Right == *this;
		}

		bool operator!=(async_iterator const& Right) const
		{
			return !(*this == Right);
		}

		T const& operator*() const
		{
			return *p->CurrentValue;
		}

		T const* operator->() const
		{
			return std::addressof(operator*());
		}

		Promise* p = nullptr;
	};

	template<typename T, typename Promise>
	struct yield_to
	{
		yield_to(Promise* p) : p(p) {}

		bool await_ready() noexcept
		{
			//std::cout << "yield_to await_ready " << std::this_thread::get_id() << std::endl;
			return false;
		}

		void await_suspend(ex::resumable_handle<> r) noexcept
		{
			//std::cout << "yield_to await_suspend " << std::this_thread::get_id() << std::endl;
			p->To = r;
			if (p->From) {
				ex::resumable_handle<> coro{ p->From };
				p->From = nullptr;

				//std::cout << "yield_to continue from" << std::endl;
				coro();
				//std::cout << "yield_to continued from" << std::endl;
			}
		}

		async_iterator<T, Promise> await_resume() noexcept
		{
			//std::cout << "yield_to await_resume " << std::this_thread::get_id() << " " << (size_t)p << std::endl;
			return{ p };
		}

		Promise* p = nullptr;
	};

	template<typename T, typename Promise>
	struct yield_from
	{
		yield_from(Promise* p) : p(p) {}

		auto& yield_wait() {
			return wait;
		}

		bool await_ready() noexcept
		{
			//std::cout << "yield_from await_ready " << std::this_thread::get_id() << std::endl;
			return false;
		}

		void await_suspend(ex::resumable_handle<> r) noexcept
		{
			//std::cout << "yield_from await_suspend " << std::this_thread::get_id() << std::endl;
			p->From = r;
			if (p->To) {
				ex::resumable_handle<> coro{ p->To };
				p->To = nullptr;
				//std::cout << "yield_from continue to" << std::endl;
				coro();
				//std::cout << "yield_from continued to" << std::endl;
			}
		}

		void await_resume() noexcept
		{
			//std::cout << "yield_from await_resume " << std::this_thread::get_id() << std::endl;
		}

		Promise* p = nullptr;
	};

	template<typename T, typename Alloc = std::allocator<char>>
	struct async_generator
	{
		struct promise_type {
			T const * CurrentValue = nullptr;
			bool done = false;
			std::exception_ptr Error;
			ex::resumable_handle<> To{ nullptr };
			ex::resumable_handle<> From{ nullptr };

			~promise_type() {
				//std::cout << "async_generator promise destroy" << std::endl;
			}
			promise_type() {
				//std::cout << "async_generator promise default" << std::endl;
			}

			promise_type& get_return_object()
			{
				//std::cout << "async_generator promise return" << std::endl;
				return *this;
			}

			ex::suspend_always initial_suspend()
			{
				//std::cout << "async_generator promise initial" << std::endl;
				return{};
			}

			ex::suspend_always final_suspend()
			{
				//std::cout << "async_generator promise final" << std::endl;
				if (To) {
					//std::cout << "async_generator promise final to" << std::endl;
					To();
					//std::cout << "async_generator promise final post to" << std::endl;
				}
				return{};
			}

			bool cancellation_requested() const
			{
				//std::cout << "async_generator promise cancelled?" << std::endl;
				return false;
			}

			void set_result()
			{
				//std::cout << "async_generator promise result" << std::endl;
				done = true;
			}

			void set_exception(std::exception_ptr Exc)
			{
				//std::cout << "async_generator promise exception" << std::endl;
				Error = std::move(Exc);
				done = true;
			}

			yield_from<T, promise_type> yield_value(T const& Value)
			{
				//std::cout << "async_generator promise yield " << Value << std::endl;
				CurrentValue = std::addressof(Value);
				return{ this };
			}
		};

		explicit async_generator(promise_type& Prom)
			: Coro(ex::resumable_handle<promise_type>::from_promise(_STD addressof(Prom)))
		{
			//std::cout << "async_generator from promise" << std::endl;
		}

		~async_generator() {
			//std::cout << "async_generator destroy" << std::endl;
		}
		async_generator() {
			//std::cout << "async_generator default" << std::endl;
		}

		async_generator(async_generator const&) = delete;

		async_generator& operator = (async_generator const&) = delete;

		async_generator(async_generator && Right)
			: Coro(Right.Coro)
		{
			//std::cout << "async_generator copy" << std::endl;
			Right.Coro = nullptr;
		}

		async_generator& operator = (async_generator && Right)
		{
			//std::cout << "async_generator assign" << std::endl;

			if (&Right != this)
			{
				Coro = Right.Coro;
				Right.Coro = nullptr;
			}
		}

		yield_to<T, promise_type> begin() {
			//std::cout << "async_generator from " << std::this_thread::get_id() << std::endl;
			Coro();
			//std::cout << "async_generator post from " << std::this_thread::get_id() << std::endl;
			return{ std::addressof(Coro.promise()) };
		}
		async_iterator<T, promise_type> end() {
			return{ nullptr };
		}

	private:
		ex::resumable_handle<promise_type> Coro = nullptr;
	};
}


namespace std {
	namespace experimental {
		template <typename T, typename Alloc, typename... Whatever>
		struct resumable_traits<async::async_generator<T, Alloc>, Whatever...> {
			using allocator_type = Alloc;
			using promise_type = typename async::async_generator<T, Alloc>::promise_type;
		};
	}
}

