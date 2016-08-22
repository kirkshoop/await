#pragma once

namespace co_alg {
	struct resume_t {};
	resume_t resume;

	template <typename T>
	struct co_iterator;

	template <typename T>
	struct co_iterator_awaiter;

	template <typename T>
	struct co_iterator_awaiter
	{
		co_iterator_awaiter(
			std::experimental::coroutine_handle<>& caller,
			std::experimental::coroutine_handle<>& yielder,
			T** value) :
			m_caller(std::addressof(caller)),
			m_yielder(std::addressof(yielder)),
			m_value(value)
		{}

		bool await_ready() {
			return false;
		}

		void await_suspend(const std::experimental::coroutine_handle<>& handle) {
			*m_caller = handle;
			if (!*m_yielder) {
				std::terminate();
			}
			*m_value = nullptr;
			(*m_yielder)();
		}

		co_iterator<T> await_resume() {
			assert(*m_value != nullptr);
			*m_caller = nullptr;
			return co_iterator<T>(*m_caller, *m_yielder, m_value);
		}

		std::experimental::coroutine_handle<>* m_caller;
		std::experimental::coroutine_handle<>* m_yielder;
		T** m_value;
	};

	template <typename T>
	struct co_iterator : std::iterator<std::input_iterator_tag, T>
	{
		co_iterator(nullptr_t) : m_caller(nullptr), m_yielder(nullptr), m_value(nullptr)
		{
		}

		co_iterator(
			std::experimental::coroutine_handle<>& caller,
			std::experimental::coroutine_handle<>& yielder,
			T** value) :
			m_caller(std::addressof(caller)),
			m_yielder(std::addressof(yielder)),
			m_value(value)
		{}

		co_iterator_awaiter<T> operator++()
		{
			return co_iterator_awaiter<T>(*m_caller, *m_yielder, m_value);
		}

		co_iterator operator++(int) = delete;
		// not implementing postincrement

		bool operator==(co_iterator const &rhs) const
		{
			return m_caller == rhs.m_caller;
		}

		bool operator!=(co_iterator const &rhs) const
		{
			return !(*this == rhs);
		}

		T &operator*()
		{
			return **m_value;
		}

		T *operator->()
		{
			return std::addressof(operator*());
		}

		T const &operator*() const
		{
			return **m_value;
		}

		T const *operator->() const
		{
			return std::addressof(operator*());
		}

		std::experimental::coroutine_handle<>* m_caller;
		std::experimental::coroutine_handle<>* m_yielder;
		T** m_value;
	};

	template <typename P>
	struct co_generator
	{
		using promise_type = P;
		using value_type = typename promise_type::value_type;
		using iterator = co_iterator<value_type>;

		co_generator(promise_type const & p) : p(std::addressof(p)) {};

		co_generator() noexcept = default;
		co_generator(const co_generator &) = delete;
		co_generator & operator=(const co_generator &) = delete;
		co_generator(co_generator &&) = default;
		co_generator & operator=(co_generator &&) = default;

		~co_generator() noexcept {
			p->destroy();
			p = nullptr;
		}

		co_iterator_awaiter<value_type> begin() const {
			return co_iterator_awaiter<value_type>(p->caller, p->yielder, std::addressof(p->value));
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
			*m_yielder = handle;
			if (!!*m_caller) {
				(*m_caller)();
			}
		}

		void await_resume() {
			if (!*m_caller) {
				std::terminate();
			}
			*m_yielder = nullptr;
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
	struct yield_value_promise
	{
		using value_type = T;

		co_caller_awaiter initial_suspend() const {
			return co_caller_awaiter(caller, yielder);
		}
		std::experimental::suspend_never final_suspend() const {
			return std::experimental::suspend_never{};
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

		void destroy() const {
			caller.done();
			caller.destroy();
			caller = nullptr;
			yielder.done();
			yielder.destroy();
			yielder = nullptr;
		}

		mutable value_type* value = nullptr;
		mutable std::experimental::coroutine_handle<> caller{};
		mutable std::experimental::coroutine_handle<> yielder{};
	};

	template<typename T>
	struct merge_value_promise
	{
		using value_type = T;

		using get = co_get_promise<merge_value_promise<T>>;

		struct complete_type
		{
			complete_type(
				const merge_value_promise<T>* that) :
				m_that(that)
			{}

			bool await_ready() {
				return !m_that->yielder && m_that->pending.empty() && m_that->sources == 0;
			}

			void await_suspend(const std::experimental::coroutine_handle<merge_value_promise<T>>& handle) {
				m_that->completer = handle;
			}

			void await_resume() {
			}

			const merge_value_promise<T>* m_that;
		};

		struct merge_caller_awaiter
		{
			merge_caller_awaiter(
				const merge_value_promise<T>* that) :
				m_that(that)
			{}

			bool await_ready() {
				return false;
			}

			void await_suspend(const std::experimental::coroutine_handle<>& handle) {
				if (!m_that->yielder) {
					m_that->pending.push_back(handle);
				}
				else {
					m_that->yielder = handle;
				}
				if (!!m_that->caller) {
					(m_that->caller)();
				}
			}

			void await_resume() {
				if (!m_that->caller) {
					std::terminate();
				}
				m_that->yielder = nullptr;
				if (!m_that->pending.empty()) {
					m_that->yielder = m_that->pending.front();
					m_that->pending.pop_front();
					if (!!m_that->caller) {
						(m_that->caller)();
					}
				}
				else {
					m_that->complete();
				}
			}

			const merge_value_promise<T>* m_that;
		};
		merge_caller_awaiter caller_awaiter() const {
			return merge_caller_awaiter(this);
		}

		merge_caller_awaiter initial_suspend() const {
			++sources;
			return caller_awaiter();
		}
		complete_type final_suspend() const {
			--sources;
			complete();
			return complete_type(this);
		}
		co_generator<merge_value_promise<value_type>> get_return_object() const {
			return co_generator<merge_value_promise<value_type>>(*this);
		}

		struct merge_source_awaiter
		{
			struct promise_type
			{
				std::experimental::suspend_never initial_suspend() const {
					return std::experimental::suspend_never{};
				}
				std::experimental::suspend_never final_suspend() const {
					--that->sources;
					that->complete();
					return std::experimental::suspend_never{};
				}
				merge_source_awaiter get_return_object() const {
					return merge_source_awaiter{};
				}

				merge_caller_awaiter yield_value(value_type& v) const {
					that->value = std::addressof(v);
					return that->caller_awaiter();
				}
				merge_caller_awaiter yield_value(value_type&& v) const {
					that->value = std::addressof(v);
					return that->caller_awaiter();
				}

				void bind(const merge_value_promise<T>* t) const {
					that = t;
					++that->sources;
				}

				mutable const merge_value_promise<T>* that;
			};

			using get = co_get_promise<promise_type>;
		};

		template<class Source>
		merge_source_awaiter push(Source s) const {
			auto& p = co_await merge_source_awaiter::get();
			p.bind(this);
			for co_await (auto& v : s) {
				co_yield v;
			}
		}

		void complete() const {
			if (!!completer && sources == 0 && !yielder && pending.empty()) {
				completer();
				completer = nullptr;
			}
		}

		void destroy() const {
			caller.done();
			caller.destroy();
			caller = nullptr;
			yielder.done();
			yielder.destroy();
			yielder = nullptr;
			completer.done();
			completer.destroy();
			completer = nullptr;
			for (auto& h : pending) {
				h.done();
				h.destroy();
				h = nullptr;
			}
			pending.clear();
		}

		mutable value_type* value = nullptr;
		mutable std::experimental::coroutine_handle<> caller{};
		mutable std::experimental::coroutine_handle<> yielder{};

		mutable int sources{};
		mutable std::deque<std::experimental::coroutine_handle<>> pending{};
		mutable std::experimental::coroutine_handle<> completer{};
	};

	template<typename Source, typename SourceValue = std::decay_t<Source>::value_type::value_type>
	co_generator<merge_value_promise<SourceValue>> merge(Source source) {
		auto& p = co_await merge_value_promise<SourceValue>::get();
		for co_await (auto&& s : source) {
			p.push(std::move(s));
		}
	}

	template<typename Source, typename Selector, typename SourceValue = std::decay_t<Source>::value_type, typename SelectValue = std::result_of_t<Selector(SourceValue const &)>>
	co_generator<yield_value_promise<SelectValue>> transform(Source source, Selector select) {
		for co_await (auto const & v : source) {
			co_yield select(v);
		}
	}

	template<typename Source, typename Predicate, typename SourceValue = std::decay_t<Source>::value_type>
	co_generator<yield_value_promise<SourceValue>> filter(Source source, Predicate predicate) {
		for co_await (auto const & v : source) {
			if (predicate(v)) {
				co_yield v;
			}
		}
	}

	co_generator<yield_value_promise<int>> ints(int first, int last) {
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

	template<typename Source, typename Bind>
	auto operator|(Source&& source, co_operator<Bind> op)  -> decltype(op.bind(std::forward<Source>(source))) {
		return op.bind(std::forward<Source>(source));
	}

}
