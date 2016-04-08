// async.cpp : Defines the entry point for the console application.
//

#include <iostream>
#include <future>

#include <string>

#include <chrono>
using namespace std::chrono;
using clk = system_clock;
using namespace std::chrono_literals;

#include <experimental/resumable>
#include <experimental/generator>
using namespace std;
using namespace std::experimental;

#define co_await __await
#define co_yield __yield_value

#include <windows.h>
#include <threadpoolapiset.h>

namespace rx {

template <typename _Ty, typename _GeneratorPromise, typename _Alloc>
struct await_iterator;

template <typename _Ty, typename _GeneratorPromise, typename _Alloc>
struct async_iterator;

template <typename _Ty, typename _GeneratorPromise, typename _Alloc >
struct await_consumer;

template <typename _Ty, typename _Alloc = allocator<char> >
struct async_generator
{
    struct promise_type
    {
        coroutine_handle<> _AwaitIteratorCoro;
        coroutine_handle<> _AwaitConsumerCoro;
        const _Ty* _CurrentValue;

        promise_type& get_return_object()
        {
            return *this;
        }

        suspend_always initial_suspend()
        {
            return{};
        }

        suspend_always final_suspend()
        {
            return{};
        }

        await_consumer<_Ty, promise_type, _Alloc> yield_value(_Ty const & _Value)
        {
            _CurrentValue = _STD addressof(_Value);
            return{ coroutine_handle<promise_type>::from_promise(this) };
        }

        void return_void() {
            auto _Coro = move(_AwaitIteratorCoro);
            _AwaitIteratorCoro = nullptr;
            if (_Coro) {
                _Coro.resume();
            }
        }

        using _Alloc_traits = allocator_traits<_Alloc>;
        using _Alloc_of_char_type = typename _Alloc_traits::template rebind_alloc<char>;

        void* operator new(size_t _Size)
        {
            _Alloc_of_char_type _Al;
            return _Al.allocate(_Size);
        }

        void operator delete(void* _Ptr, size_t _Size) _NOEXCEPT
        {
            _Alloc_of_char_type _Al;
            return _Al.deallocate(static_cast<char*>(_Ptr), _Size);
        }
    };

    await_iterator<_Ty, promise_type, _Alloc> begin()
    {
        return _Coro;
    }

    async_iterator<_Ty, promise_type, _Alloc> end()
    {
        return{ nullptr };
    }

    explicit async_generator(promise_type& _Prom)
        : _Coro(coroutine_handle<promise_type>::from_promise(_STD addressof(_Prom)))
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
            _Coro.destroy();
        }
    }
private:
    coroutine_handle<promise_type> _Coro = nullptr;
};

template <typename _Ty, typename _GeneratorPromise, typename _Alloc >
struct await_consumer
{
    coroutine_handle<_GeneratorPromise> _GeneratorCoro;

    await_consumer(coroutine_handle<_GeneratorPromise> _GCoro)
        : _GeneratorCoro(_GCoro)
    {
    }

    await_consumer() = default;

    await_consumer(await_consumer const&) = delete;

    await_consumer& operator = (await_consumer const&) = delete;

    await_consumer(await_consumer && _Right)
        : _GeneratorCoro(_Right._GeneratorCoro)
    {
        _Right._GeneratorCoro = nullptr;
    }

    await_consumer& operator = (await_consumer && _Right)
    {
        if (&_Right != this)
        {
            _GeneratorCoro = _Right._GeneratorCoro;
            _Right._GeneratorCoro = nullptr;
        }
        return *this;
    }

    ~await_consumer()
    {
    }

    bool await_ready() _NOEXCEPT
    {
        return false;
    }

    void await_suspend(coroutine_handle<> _AwaitConsumerCoro) _NOEXCEPT
    {
        _GeneratorCoro.promise()._AwaitConsumerCoro = _AwaitConsumerCoro;

        auto _AwaitIteratorCoro = move(_GeneratorCoro.promise()._AwaitIteratorCoro);
        _GeneratorCoro.promise()._AwaitIteratorCoro = nullptr;
        if (_AwaitIteratorCoro) {
            _AwaitIteratorCoro.resume();
        }
    }

    void await_resume() _NOEXCEPT
    {
    }
};

template <typename _Ty, typename _GeneratorPromise, typename _Alloc >
struct await_iterator 
{
    coroutine_handle<_GeneratorPromise> _GeneratorCoro;
    async_iterator<_Ty, _GeneratorPromise, _Alloc>* _It;
    bool owner;

    await_iterator(coroutine_handle<_GeneratorPromise> _GCoro)
        : _GeneratorCoro(_GCoro)
        , _It(nullptr)
    {
    }

    // operator++ needs to update itself
    await_iterator(async_iterator<_Ty, _GeneratorPromise, _Alloc>* _OIt)
        : _GeneratorCoro(_OIt->_GeneratorCoro)
        , _It(_OIt)
    {
    }

    await_iterator() 
        : _GeneratorCoro()
        , _It(nullptr)
    {}

    await_iterator(await_iterator const&) = delete;

    await_iterator& operator = (await_iterator const&) = delete;

    await_iterator(await_iterator && _Right)
        : _GeneratorCoro(_Right._GeneratorCoro)
        , _It(_OIt)
    {
        _Right._GeneratorCoro = nullptr;
        _Right._It = nullptr;
    }

    await_iterator& operator = (await_iterator && _Right)
    {
        if (&_Right != this)
        {
            _GeneratorCoro = _Right._GeneratorCoro;
            _Right._GeneratorCoro = nullptr;

            _It = _Right._It;
            _Right._It = nullptr;
        }
        return *this;
    }

    ~await_iterator()
    {
    }

    bool await_ready() _NOEXCEPT
    {
        return false;
    }

    void await_suspend(coroutine_handle<> _AwaitIteratorCoro) _NOEXCEPT
    {
        _GeneratorCoro.promise()._AwaitIteratorCoro = _AwaitIteratorCoro;

        auto _AwaitConsumerCoro = move(_GeneratorCoro.promise()._AwaitConsumerCoro);
        _GeneratorCoro.promise()._AwaitConsumerCoro = nullptr;
        if (_AwaitConsumerCoro) {
            // resume co_yield
            _GeneratorCoro.promise()._CurrentValue = nullptr;
            _AwaitConsumerCoro.resume();
        }
        else {
            // first resume
            _GeneratorCoro.resume();
        }

    }

    async_iterator<_Ty, _GeneratorPromise, _Alloc> await_resume() _NOEXCEPT
    {
        if (_GeneratorCoro.done() || !_GeneratorCoro.promise()._CurrentValue) {
            _GeneratorCoro = nullptr;
        }
        if (_It) {
            _It->_GeneratorCoro = _GeneratorCoro;
            return{*_It};
        }
        return{ _GeneratorCoro };
    }
};

template <typename _Ty, typename _GeneratorPromise, typename _Alloc>
struct async_iterator
    : _STD iterator<input_iterator_tag, _Ty>
{
    coroutine_handle<_GeneratorPromise> _GeneratorCoro;

    async_iterator(nullptr_t)
        : _GeneratorCoro(nullptr)
    {
    }

    async_iterator(coroutine_handle<_GeneratorPromise> _GCoro)
        : _GeneratorCoro(_GCoro)
    {
    }

    await_iterator<_Ty, _GeneratorPromise, _Alloc> operator++()
    {
        if (!_GeneratorCoro) abort();
        return{this};
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

    bool operator==(async_iterator const& _Right) const
    {
        return _GeneratorCoro == _Right._GeneratorCoro;
    }

    bool operator!=(async_iterator const& _Right) const
    {
        return !(*this == _Right);
    }

    _Ty const& operator*() const
    {
        return *_GeneratorCoro.promise()._CurrentValue;
    }

    _Ty const* operator->() const
    {
        return _STD addressof(operator*());
    }

};

}

// usage: await resume_at(std::chrono::system_clock::now() + 1s);
auto resume_at(clk::time_point at) {
    class awaiter {
        static void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, void* Context, PTP_TIMER) {
            coroutine_handle<>::from_address(Context)();
        }
        PTP_TIMER timer = nullptr;
        clk::time_point at;
    public:
        awaiter(clk::time_point a)
            : at(a) {}
        bool await_ready() const {
            return clk::now() >= at;
        }
        void await_suspend(coroutine_handle<> resume_cb) {
            auto duration = at - clk::now();
            int64_t relative_count = -duration.count();
            timer = CreateThreadpoolTimer(TimerCallback, resume_cb.to_address(), nullptr);
            if (timer == 0)
                throw system_error(GetLastError(), system_category());
            SetThreadpoolTimer(timer, (PFILETIME)&relative_count, 0, 0);
        }
        void await_resume() {
        }
        ~awaiter() {
            if (timer) CloseThreadpoolTimer(timer);
        }
    };
    return awaiter{ at };
}

// usage: await resume_after(1s);
auto resume_after(clk::duration period) {
    return resume_at(clk::now() + period);
}

rx::async_generator<int> fibonacci(int n) {
    int a = 0;
    int b = 1;

    while (n-- > 0) {
        co_yield a;
        auto next = a + b; a = b;
        b = next;
    }
}

template<class Adaptor>
struct adaptor
{
    mutable decay_t<Adaptor> a;

    template<class T, class Alloc>
    auto operator()(rx::async_generator<T, Alloc> s) const -> 
        result_of_t<decay_t<Adaptor>(rx::async_generator<T, Alloc>)> {
        return a(move(s));
    }
};

template<class Adaptor>
auto make_adaptor(Adaptor&& a) -> adaptor<decay_t<Adaptor>> {
    return{ forward<Adaptor>(a) };
}

namespace detail {

    struct delay
    {
        clk::duration period;

        template<class T, class Alloc>
        auto operator()(rx::async_generator<T, Alloc> s) const -> rx::async_generator<T, Alloc> {
            for co_await(auto i : s) {
                co_await resume_after(period);
                co_yield i;
            }
        }
    };
}

auto delay(clk::duration p) -> adaptor<detail::delay> {
    return make_adaptor(detail::delay{ p });
}

namespace detail {

    template<class Pred>
    struct copy_if
    {
        mutable decay_t<Pred> pred;

        template<class T, class Alloc>
        auto operator()(rx::async_generator<T, Alloc> s) const -> rx::async_generator<T, Alloc> {
            for co_await(auto i : s) {
                if (pred(i)) co_yield i;
            }
        }
    };
}

template<class Pred>
auto copy_if(Pred&& p) -> adaptor<detail::copy_if<Pred>> {
    return make_adaptor(detail::copy_if<Pred>{forward<Pred>(p)});
}

namespace detail {

    template<class Transform>
    struct transform
    {
        mutable decay_t<Transform> t;

        template<class T, class Alloc>
        auto operator()(rx::async_generator<T, Alloc> s) const -> 
            rx::async_generator<result_of_t<decay_t<Transform>(T)>, Alloc> {
            for co_await(auto v : s) {
                co_yield t(v);
            }
        }
    };
}

template<class Pred>
auto transform(Pred&& p) -> adaptor<detail::transform<Pred>> {
    return make_adaptor(detail::transform<Pred>{forward<Pred>(p)});
}

template<class T, class Alloc, class Adaptor>
auto operator|(rx::async_generator<T, Alloc> s, adaptor<Adaptor> adapt) -> 
    result_of_t<decay_t<Adaptor>(rx::async_generator<T, Alloc>)> {
    return adapt(move(s));
}

future<void> waitfor() {
    for co_await(auto v : fibonacci(10) | 
        copy_if([](int v) {return v % 2 != 0; }) |
        transform([](int i) {return to_string(i) + ","; }) | 
        delay(1s)
    ) {
        std::cout << v << ' ';
    }
}

int wmain() {
    waitfor().get();
}
