#pragma once

/***
*async_generator
*
*       Copyright (c) Microsoft Corporation. All rights reserved.
*
*Purpose: Library support of stackless resumable functions. async_generator class
*         http://isocpp.org/files/papers/N4134.pdf
*
*       [Public]
*
****/
#pragma once
#ifndef _ASYNC_GENERATOR_
#define _ASYNC_GENERATOR_
#ifndef RC_INVOKED

#ifndef _RESUMABLE_FUNCTIONS_SUPPORTED
#error <experimental/async_generator> requires /await compiler option
#endif /* _RESUMABLE_FUNCTIONS_SUPPORTED */

#include <experimental/resumable>

#pragma pack(push,_CRT_PACKING)
#pragma push_macro("new")
#undef new

_STD_BEGIN

namespace experimental {

	template <typename _Ty, typename _Iterator>
	struct async_iterator
	{
		struct promise_type
		{
			_Ty const * _CurrentValue;

			exception_ptr _Error;

			enum class _StateT { _Active, _Cancelling, _Closed };

			_StateT _State = _StateT::_Active;

			resumable_handle<> step;

			promise_type& get_return_object()
			{
				std::cout << "promise" << std::endl;
				return *this;
			}

			suspend_always initial_suspend()
			{
				std::cout << "initial" << std::endl;
				return{};
			}

			suspend_always final_suspend()
			{
				std::cout << "final" << std::endl;
				_State = _StateT::_Closed;
				return{};
			}

			bool cancellation_requested() const
			{
				std::cout << "cancel" << std::endl;
				return _State == _StateT::_Cancelling;
			}

			void set_result()
			{
				std::cout << "result set" << std::endl;
			}

			void set_exception(exception_ptr _Exc)
			{
				_Error = _STD move(_Exc);
			}

			suspend_never yield_value(_Ty const& _Value)
			{
				_CurrentValue = _STD addressof(_Value);
				std::cout << "step" << std::endl;
				step();
				std::cout << "stepped" << std::endl;
				return{};
			}
		};

		bool await_ready() noexcept
		{
			return false;
		}

		void await_suspend(resumable_handle<> s) noexcept
		{
			if (i && i->_Coro)
			{
				i->_Coro.promise().step = s;

				std::cout << "coro" << std::endl;
				i->_Coro();
				std::cout << "coroed" << std::endl;

				if (i->_Coro.promise()._Error)
					_STD rethrow_exception(i->_Coro.promise()._Error);

				if (i->_Coro.promise()._State == promise_type::_StateT::_Closed)
					*i = _Iterator{ nullptr };
			}
		}

		std::unique_ptr<_Iterator> i;

		_Iterator await_resume() noexcept
		{
			std::cout << "resume" << std::endl;
			return *i.get();
		}

		async_iterator(_Iterator& i) : i(new _Iterator(std::move(i))) {}
		async_iterator(nullptr_t) : i(new _Iterator(nullptr)) {}
	};

	template <typename _Ty, typename _Alloc = allocator<char> >
	struct async_generator
	{
		struct iterator
			: _STD iterator<input_iterator_tag, _Ty>
		{
			using async_iterator = typename async_iterator<_Ty, iterator>;

			using promise_type = typename async_iterator::promise_type;

			resumable_handle<promise_type> _Coro;

			iterator(nullptr_t)
				: _Coro(nullptr)
			{
				std::cout << "end it" << std::endl;
			}

			iterator(resumable_handle<promise_type> _CoroArg)
				: _Coro(_CoroArg)
			{
				std::cout << "it" << std::endl;
			}

			async_iterator operator++()
			{
				std::cout << "inc" << std::endl;
				return{ *this };
			}

			async_iterator operator++(int) = delete;
			// async_generator iterator current_value
			// is a reference to a temporary on the coroutine frame
			// implementing postincrement will require storing a copy
			// of the value in the iterator.
			//{
			//	auto _Result = *this;
			//	++(*this);
			//	return _Result;
			//}

			bool operator==(iterator const& _Right) const
			{
				std::cout << "compare" << std::endl;
				return _Coro == _Right._Coro;
			}

			bool operator!=(iterator const& _Right) const
			{
				return !(*this == _Right);
			}

			_Ty const& operator*() const
			{
				std::cout << "deref" << std::endl;
				auto& _Prom = _Coro.promise();
				if (_Prom._Error)
					_STD rethrow_exception(_Prom._Error);
				return *_Prom._CurrentValue;
			}

			_Ty const* operator->() const
			{
				return _STD addressof(operator*());
			}

		};

		using promise_type = typename iterator::promise_type;

		typename iterator::async_iterator begin()
		{
			std::cout << "begin" << std::endl;
			return iterator{ _Coro };
		}

		iterator end()
		{
			std::cout << "end" << std::endl;
			return{ nullptr };
		}

		explicit async_generator(promise_type& _Prom)
			: _Coro(resumable_handle<promise_type>::from_promise(_STD addressof(_Prom)))
		{
		}

		async_generator() = default;

		async_generator(async_generator const&) = delete;

		async_generator& operator = (async_generator const&) = delete;

		async_generator(async_generator && _Right)
			: _Coro(_Right._Coro)
		{
			_Right._Coro = nullptr;
		}

		async_generator& operator = (async_generator && _Right)
		{
			if (&_Right != this)
			{
				_Coro = _Right._Coro;
				_Right._Coro = nullptr;
			}
		}

		~async_generator()
		{
			if (_Coro)
			{
				auto& _Prom = _Coro.promise();
				if (_Prom._State == promise_type::_StateT::_Active)
				{
					// Note: on the cancel path, we resume the coroutine twice.
					// Once to resume at the current point and force cancellation.
					// Second, to move beyond the final_suspend point.
					//
					// Alternative design would be to check in final_suspend whether
					// the state is being cancelled and return true from "await_ready",
					// thus bypassing the final suspend.
					//
					// Current design favors normal path. Alternative, cancel path.

					_Prom._State = promise_type::_StateT::_Cancelling;
					_Coro();
				}
				_Coro();
			}
		}
	private:
		resumable_handle<promise_type> _Coro = nullptr;
	};

	template <typename _Ty, typename _Alloc, typename... _Whatever>
	struct resumable_traits<async_generator<_Ty, _Alloc>, _Whatever...> {
		using allocator_type = _Alloc;
		using promise_type = typename async_generator<_Ty, _Alloc>::promise_type;
	};


} // namespace experimental

_STD_END

#pragma pop_macro("new")
#pragma pack(pop)
#endif /* RC_INVOKED */
#endif /* _ASYNC_GENERATOR_ */
