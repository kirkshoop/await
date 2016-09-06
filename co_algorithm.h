#pragma once

namespace co_alg {

	// extension point for switching from exceptions to error codes

	template<typename T>
	struct co_exception;

	template<typename T>
	struct co_exception
	{
		mutable std::exception_ptr error = nullptr;
		void set(std::exception_ptr ep) const {
			error = ep;
		}
		T* yield() const {
			return nullptr;
		}
		void resume() const {
			auto e = error;
			error = nullptr;
			if (e) {
				std::rethrow_exception(e);
			}
		}
	};

	template<typename T>
	struct co_generator_promise
	{
		using value_type = T;

		mutable value_type* value = nullptr;
		mutable std::experimental::coroutine_handle<> caller{};
		mutable std::experimental::coroutine_handle<> yielder{};
		co_exception<T> error;
	};

	template <typename T>
	struct co_iterator;

	template <typename T>
	struct co_inc_awaiter
	{
		co_inc_awaiter(co_iterator<T>& it) :
			m_it(std::addressof(it))
		{}

		bool await_ready() {
			return false;
		}

		void await_suspend(const std::experimental::coroutine_handle<>& handle) {
			if (!!m_it->m_p->caller) {
				std::terminate();
			}
			m_it->m_p->caller = handle;
			auto yielder = m_it->m_p->yielder;
			m_it->m_p->yielder = nullptr;
			if (!yielder) {
				std::terminate();
			}
			m_it->m_p->value = nullptr;
			yielder();
		}

		co_iterator<T>& await_resume() {
			m_it->m_p->error.resume();
			if (!m_it->m_p->value) {
				// end iterator
				*m_it = co_iterator<T>(nullptr);
			}
			return *m_it;
		}

		co_iterator<T>* m_it;
	};

	template <typename T>
	struct co_iterator : std::iterator<std::input_iterator_tag, T>
	{
		// end iterator
		co_iterator(nullptr_t) : m_p(nullptr)
		{
		}

		// iterator
		co_iterator(co_generator_promise<T> const & p) :
			m_p(std::addressof(p))
		{}

		co_inc_awaiter<T> operator++()
		{
			auto result = co_inc_awaiter<T>(*this);
			return result;
		}

		co_iterator operator++(int) = delete;
		// not implementing postincrement

		bool operator==(co_iterator const &rhs) const
		{
			return m_p == rhs.m_p;
		}

		bool operator!=(co_iterator const &rhs) const
		{
			return !(*this == rhs);
		}

		T &operator*()
		{
			return *m_p->value;
		}

		T *operator->()
		{
			return std::addressof(operator*());
		}

		T const &operator*() const
		{
			return *m_p->value;
		}

		T const *operator->() const
		{
			return std::addressof(operator*());
		}

		co_generator_promise<T> const * m_p;
	};

	template <typename T>
	struct co_iterator_awaiter
	{
		co_iterator_awaiter(co_generator_promise<T> const & p) :
			m_p(std::addressof(p))
		{}

		bool await_ready() {
			return false;
		}

		void await_suspend(const std::experimental::coroutine_handle<>& handle) {
			if (!!m_p->caller) {
				std::terminate();
			}
			m_p->caller = handle;
			auto yielder = m_p->yielder;
			m_p->yielder = nullptr;
			if (!yielder) {
				std::terminate();
			}
			m_p->value = nullptr;
			yielder();
		}

		co_iterator<T> await_resume() {
			m_p->error.resume();
			if (!m_p->value) {
				// end iterator
				return co_iterator<T>(nullptr);
			}
			return co_iterator<T>(*m_p);
		}

		co_generator_promise<T> const * m_p;
	};

	template <typename P>
	struct co_generator
	{
		using promise_type = P;
		using value_type = typename promise_type::value_type;
		using iterator = co_iterator<value_type>;

		co_generator(promise_type const & p) : p(std::addressof(p)) {};

		co_generator() noexcept = default;
		co_generator(const co_generator &) = default;
		co_generator & operator=(const co_generator &) = default;
		co_generator(co_generator && o) : p(o.p) {
			o.p = nullptr;
		}
		co_generator & operator=(co_generator && o) {
			p = o.p;
			o.p = nullptr;
			return *this;
		}

		~co_generator() noexcept {
			//if (!!p) {
			//	p->destroy();
			//	p = nullptr;
			//}
		}

		co_iterator_awaiter<value_type> begin() const {
			return co_iterator_awaiter<value_type>(*p);
		}

		co_iterator<value_type> end() const {
			return co_iterator<value_type>(nullptr);
		}

	private:
		promise_type const * p;
	};

	struct co_caller_awaiter
	{
		co_caller_awaiter(
			std::experimental::coroutine_handle<>& caller,
			std::experimental::coroutine_handle<>& yielder) :
			m_caller(std::addressof(caller)),
			m_yielder(std::addressof(yielder))
		{}

		bool await_ready() {
			return false;
		}

		void await_suspend(const std::experimental::coroutine_handle<>& handle) {
			if (!!*m_yielder) {
				std::terminate();
			}
			*m_yielder = handle;
			auto caller = *m_caller;
			*m_caller = nullptr;
			if (!!caller) {
				caller();
			}
		}

		void await_resume() {
		}

		std::experimental::coroutine_handle<>* m_caller;
		std::experimental::coroutine_handle<>* m_yielder;
	};

	template<typename Promise>
	struct co_get_promise
	{
		using promise_type = Promise;

		bool await_ready() {
			return false;
		}

		void await_suspend(const std::experimental::coroutine_handle<promise_type>& handle) {
			p = std::addressof(handle.promise());
			handle();
		}

		const promise_type& await_resume() {
			return *p;
		}

		const promise_type* p;
	};

	template<typename Bind>
	struct co_operator
	{
		Bind bind;
	};

	template<typename Bind>
	co_operator<Bind> make_operator(Bind bind) {
		return co_operator<Bind>{bind};
	}

	template<typename T>
	struct yield_value_promise : co_generator_promise<T>
	{
		std::future<void> emit_error() const {
			value = error.yield();
			if (value) {
				co_await co_caller_awaiter(caller, yielder);
			}
		}
		co_caller_awaiter initial_suspend() const {
			return co_caller_awaiter(caller, yielder);
		}
		co_caller_awaiter final_suspend() const {
			emit_error().get();
			// emit end iterator
			value = nullptr;
			return co_caller_awaiter(caller, yielder);
		}
		co_generator<yield_value_promise<value_type>> get_return_object() const {
			return co_generator<yield_value_promise<value_type>>(*this);
		}

		co_caller_awaiter yield_value(value_type& v) const {
			value = std::addressof(v);
			return co_caller_awaiter(caller, yielder);
		}
		co_caller_awaiter yield_value(value_type&& v) const {
			value = std::addressof(v);
			return co_caller_awaiter(caller, yielder);
		}
		void return_void() const {
			assert(value == nullptr);
		}
		void set_exception(std::exception_ptr ep) const {
			error.set(ep);
		}

		void destroy() const {
			auto y = yielder;
			yielder = nullptr;
			if (y) {
				y.destroy();
			}
		}
	};

	template<typename T>
	using co_value_generator = co_generator<yield_value_promise<T>>;

	template<typename T>
	struct merge_value_promise : co_generator_promise<T>
	{
		using value_type = T;

		using get = co_get_promise<merge_value_promise<T>>;

		struct merge_caller_awaiter
		{
			merge_caller_awaiter(
				const merge_value_promise<T>* that,
				bool* canceled) :
				m_that(that),
				m_canceled(canceled)
			{}

			bool await_ready() {
				return false;
			}

			void await_suspend(const std::experimental::coroutine_handle<>& handle) {
				if (!!m_that->yielder) {
					m_that->pending.push_back(handle);
				}
				else {
					m_that->yielder = handle;
				}
				auto c = m_that->caller;
				m_that->caller = nullptr;
				if (!!c) {
					c();
				}
			}

			void await_resume() {
				if (m_canceled && *m_canceled) {
					return;
				}
				if (!m_that->caller) {
					std::terminate();
				}
				m_that->yielder = nullptr;
				if (!m_that->pending.empty()) {
					m_that->yielder = m_that->pending.front();
					m_that->pending.pop_front();
					auto c = m_that->caller;
					m_that->caller = nullptr;
					c();
				}
			}

			const merge_value_promise<T>* m_that;
			bool* m_canceled;
		};
		merge_caller_awaiter caller_awaiter(bool* canceled) const {
			return merge_caller_awaiter(this, canceled);
		}

		struct merge_complete_awaiter
		{
			merge_complete_awaiter(
				const merge_value_promise<T>* that) :
				m_that(that)
			{}

			bool await_ready() {
				return m_that->sources == 0 && !m_that->yielder && m_that->pending.empty();
			}

			void await_suspend(const std::experimental::coroutine_handle<>& handle) {
				m_that->completer = handle;
			}

			std::future<void> emit_error() const {
				m_that->value = m_that->error.yield();
				if (m_that->value) {
					co_await m_that->caller_awaiter(nullptr);
				}
			}

			void await_resume() {
				if (!m_that->caller) {
					std::terminate();
				}

				m_that->stop();

				emit_error().get();

				// resume with end iterator
				m_that->value = nullptr;
				auto c = m_that->caller;
				m_that->caller = nullptr;
				c();
			}

			const merge_value_promise<T>* m_that;
		};
		merge_complete_awaiter complete_awaiter() const {
			return merge_complete_awaiter(this);
		}

		std::experimental::suspend_never initial_suspend() const {
			++sources;
			return std::experimental::suspend_never{};
		}
		merge_complete_awaiter final_suspend() const {
			--sources;
			return complete_awaiter();
		}
		co_generator<merge_value_promise<value_type>> get_return_object() const {
			return co_generator<merge_value_promise<value_type>>(*this);
		}
		void set_exception(std::exception_ptr ep) const {
			error.set(ep);
			stop();
		}

		struct merge_source_awaiter
		{
			struct promise_type
			{
				std::experimental::suspend_never initial_suspend() const {
					return std::experimental::suspend_never{};
				}
				std::experimental::suspend_never final_suspend() const {
					if (!*canceled) {
						--that->sources;
						that->cancels.erase(canceled);
						that->complete();
					}
					return std::experimental::suspend_never{};
				}
				merge_source_awaiter get_return_object() const {
					return merge_source_awaiter{};
				}

				merge_caller_awaiter yield_value(value_type& v) const {
					assert(!*canceled);
					that->value = std::addressof(v);
					return that->caller_awaiter(canceled);
				}
				merge_caller_awaiter yield_value(value_type&& v) const {
					assert(!*canceled);
					that->value = std::addressof(v);
					return that->caller_awaiter(canceled);
				}
				void set_exception(std::exception_ptr ep) const {
					assert(!*canceled);
					that->error.set(ep);
					that->stop();
				}

				void bind(const merge_value_promise<T>* t, bool& c) const {
					that = t;
					++that->sources;
					canceled = std::addressof(c);
					that->cancels.insert(canceled);
				}

				mutable const merge_value_promise<T>* that;
				mutable bool* canceled;
			};

			using get = co_get_promise<promise_type>;
		};

		template<class Source>
		merge_source_awaiter push(Source s) const {
			bool canceled = false;
			auto& p = co_await merge_source_awaiter::get();
			p.bind(this, canceled);
			for co_await (auto& v : s) {
				if (canceled) {
					break;
				}
				co_yield v;
				if (canceled) {
					break;
				}
			}
		}

		void complete() const {
			if (!!completer && sources == 0 && !yielder && pending.empty()) {
				auto c = completer;
				completer = nullptr;
				c();
			}
		}

		void stop() const {
			for (auto c : cancels) {
				*c = true;
			}
			cancels.clear();
			auto y = yielder;
			yielder = nullptr;
			if (y) {
				y();
			}
			auto p = pending;
			pending.clear();
			for (auto& h : p) {
				h();
			}
		}

		void destroy() const {
			stop();
			complete();
		}

		mutable int sources{};
		mutable std::deque<std::experimental::coroutine_handle<>> pending{};
		mutable std::experimental::coroutine_handle<> completer{};
		mutable std::set<bool*> cancels;
	};

	template<typename Source, typename SourceValue = std::decay_t<Source>::value_type::value_type>
	co_generator<merge_value_promise<SourceValue>> merge(Source source) {
		auto& p = co_await merge_value_promise<SourceValue>::get();
		co_await p.caller_awaiter(nullptr);
		for co_await (auto&& s : source) {
			p.push(std::move(s));
		}
	}

	template<typename Source, typename SourceValue = std::decay_t<Source>::value_type::value_type>
	co_value_generator<SourceValue> concat(Source source) {
		for co_await (auto&& s : source) {
			for co_await (auto&& v : s) {
				co_yield v;
			}
		}
	}

	template<typename Trigger>
	std::future<void> pulltrigger(Trigger trigger, bool& triggered, bool*& cancelTrigger) {
		bool canceled = false;
		cancelTrigger = &canceled;
		for co_await (auto&& v : trigger) {
			if (canceled) {
				return;
			}
			triggered = true;
		}
		cancelTrigger = nullptr;
	}

	template<typename Source, typename Trigger, typename SourceValue = std::decay_t<Source>::value_type>
	co_value_generator<SourceValue> take_until(Source source, Trigger trigger) {
		bool triggered = false;
		bool* cancelTrigger = nullptr;
		pulltrigger(trigger, triggered, cancelTrigger);
		for co_await (auto&& v : source) {
			if (triggered) {
				return;
			}
			co_yield v;
		}
		if (!!cancelTrigger) {
			*cancelTrigger = true;
		}
	}

	template<typename Source, typename Selector, typename SourceValue = std::decay_t<Source>::value_type, typename SelectValue = std::result_of_t<Selector(SourceValue const &)>>
	co_value_generator<SelectValue> transform(Source source, Selector select) {
		for co_await (auto&& v : source) {
			co_yield select(v);
		}
	}

	template<typename Source, typename Predicate, typename SourceValue = std::decay_t<Source>::value_type>
	co_value_generator<SourceValue> filter(Source source, Predicate predicate) {
		for co_await (auto&& v : source) {
			if (predicate(std::cref(v).get())) {
				co_yield v;
			}
		}
	}

	template<typename Source, typename SourceValue = std::decay_t<Source>::value_type>
	co_value_generator<SourceValue> take(Source source, ptrdiff_t count) {
		if (count == 0) {
			return;
		}
		for co_await (auto&& v : source) {
			co_yield v;
			if (--count == 0) {
				break;
			}
		}
	}

	template<typename Source, typename SourceValue = std::decay_t<Source>::value_type>
	co_value_generator<SourceValue> skip(Source source, ptrdiff_t count) {
		for co_await (auto&& v : source) {
			if (count == 0) {
				co_yield v;
				continue;
			}
			--count;
		}
	}

	template<typename Exception, typename Source, typename Selector, typename SourceValue = std::decay_t<Source>::value_type>
	co_value_generator<SourceValue> resume_error(Source source, Selector select) {
		Exception e;
		bool error = false;
		try 
		{
			for co_await (auto&& v : source) {
				co_yield v;
			}
		}
		catch (const Exception& ex) 
		{
			e = ex;
			error = true;
		}
		if (error) {
			auto s = select(e);
			for co_await (auto&& v : s) {
				co_yield v;
			}
		}
	}

	template<typename T>
	co_value_generator<T> empty() {
	}

	template<typename T>
	co_value_generator<T> never() {
		for (;;) { 
			co_await std::experimental::suspend_always{}; 
		}
	}

	co_value_generator<int> ints(int first, int last) {
		for (int cursor = first;; ++cursor) {
			co_yield cursor;
			if (cursor == last) break;
		}
	}

	auto merge() {
		return make_operator([=](auto&& source) {
			return merge(std::forward<decltype(source)>(source));
		});
	}

	auto concat() {
		return make_operator([=](auto&& source) {
			return concat(std::forward<decltype(source)>(source));
		});
	}

	template<typename Selector>
	auto transform(Selector select) {
		return make_operator([=](auto&& source) {
			return transform(std::forward<decltype(source)>(source), select);
		});
	}

	template<typename Predicate>
	auto filter(Predicate predicate) {
		return make_operator([=](auto&& source) {
			return filter(std::forward<decltype(source)>(source), predicate);
		});
	}

	template<typename Trigger>
	auto take_until(Trigger trigger) {
		return make_operator([=](auto&& source) {
			return take_until(std::forward<decltype(source)>(source), trigger);
		});
	}

	auto take(ptrdiff_t count) {
		return make_operator([=](auto&& source) {
			return take(std::forward<decltype(source)>(source), count);
		});
	}

	auto skip(ptrdiff_t count) {
		return make_operator([=](auto&& source) {
			return skip(std::forward<decltype(source)>(source), count);
		});
	}

	template<typename Exception, typename Selector>
	auto resume_error(Selector select) {
		return make_operator([=](auto&& source) {
			return resume_error<Exception>(std::forward<decltype(source)>(source), select);
		});
	}

	template<typename Source, typename Bind>
	auto operator|(Source&& source, co_operator<Bind> op)  -> decltype(op.bind(std::forward<Source>(source))) {
		return op.bind(std::forward<Source>(source));
	}

}
