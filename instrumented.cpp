
// instrumented.cpp : Defines the entry point for the console application.
//
// In VS 2015 x64 Native prompt:
// CL.exe /Zi /nologo /W3 /sdl- /Od /D _DEBUG /D WIN32 /D _CONSOLE /D _UNICODE /D UNICODE /Gm /EHsc /MDd /GS /fp:precise /Zc:wchar_t /Zc:forScope /Gd /TP /await instrumented.cpp
//

#include <iostream>
#include <iomanip>
#include <future>
#include <sstream>

#include <chrono>
namespace t = std::chrono;
using clk = t::system_clock;
using namespace std::chrono_literals;

#include <experimental/resumable>
#include <experimental/generator>
namespace ex = std::experimental;


#include <windows.h>
#include <threadpoolapiset.h>

std::mutex lock;

template<class... T>
void outln(T&&... t) {
    std::unique_lock<std::mutex> guard(lock);
    //std::cout << std::this_thread::get_id();
    int seq[] = {(std::cout << std::forward<T>(t) , 0)...};
    std::cout << std::endl;
}

template<class From, class To, class... TN>
void out_call(From&& f, To&& t, TN&&... tn) {
    outln('\"', std::forward<From>(f), "\" -> \"", std::forward<To>(t), "\" : \"", std::forward<TN>(tn)..., '\"');
}

template<class Color, class From, class To, class... TN>
void out_call_color(Color&& c, From&& f, To&& t, TN&&... tn) {
    outln('\"', std::forward<From>(f), "\" -[#", std::forward<Color>(c), "]> \"", std::forward<To>(t), "\" : \"", std::forward<TN>(tn)..., '\"');
}

template<class From, class To, class... TN>
void out_return(To&& t, From&& f, TN&&... tn) {
    outln('\"', std::forward<To>(t), "\" <-- \"", std::forward<From>(f), "\" : \"", std::forward<TN>(tn)..., '\"');
}

template<class Color, class From, class To, class... TN>
void out_return_color(Color&& c, To&& t, From&& f, TN&&... tn) {
    outln('\"', std::forward<To>(t), "\" <[#", std::forward<Color>(c), "]-- \"", std::forward<From>(f), "\" : \"", std::forward<TN>(tn)..., '\"');
}

template<class To, class... TN>
void out_from_compiler(To&& t, TN&&... tn) {
    outln("\"compiler\" -> \"", std::forward<To>(t), "\" : \"", std::forward<TN>(tn)..., '\"');
}

template<class From, class... TN>
void out_to_compiler(From&& f, TN&&... tn) {
    outln("\"compiler\"<-- \"", std::forward<From>(f), "\" : \"", std::forward<TN>(tn)..., '\"');
}

template<class... TN>
void out_group_begin(TN&&... tn) {
    outln("group \"", std::forward<TN>(tn)..., '\"');
}
void out_group_end() {
    outln("end");
}

template<class To, class Message, class Result>
void out_called(To t, Message&& m, Result&& r) {
    out_from_compiler(t, std::forward<Message>(m));
    out_to_compiler(t, std::forward<Result>(r));
}

namespace async {

    namespace ex = std::experimental;

    struct suspend_always
    {
        ~suspend_always() {
            outln("deactivate suspend_always");
        }
        suspend_always() {
            outln("activate suspend_always");
        }

        bool await_ready() noexcept
        {
            out_called("suspend_always", "await_ready()", "false");
            return false;
        }

        void await_suspend(ex::resumable_handle<>) noexcept
        {
            out_called("suspend_always", "await_suspend(resumable_handle<>)", "void");
        }

        void await_resume() noexcept
        {
            out_called("suspend_always", "await_resume()", "void");
        }
    };


    template<typename T, typename Promise>
    struct yield_to;

    template<typename T, typename Promise>
    struct async_iterator : public std::iterator<std::input_iterator_tag, T>
    {
        ~async_iterator() {
            outln("deactivate async_iterator");
        }
        async_iterator(Promise* p) : p(p) {
            outln("activate async_iterator");
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
        //  auto _Result = *this;
        //  ++(*this);
        //  return _Result;
        //}

        bool operator==(async_iterator const& Right) const
        {
            return !!p ? p->done || p == Right.p : p == Right.p || Right == *this;
        }

        bool operator!=(async_iterator const& Right) const
        {
            return !(*this == Right);
        }

        T const& operator*() const
        {
            if (p->Error) {
                std::rethrow_exception(p->Error);
            }
            return *p->CurrentValue;
        }

        T& operator*()
        {
            if (p->Error) {
                std::rethrow_exception(p->Error);
            }
            return *p->CurrentValue;
        }

        T const* operator->() const
        {
            return std::addressof(operator*());
        }

        T* operator->()
        {
            return std::addressof(operator*());
        }

        Promise* p = nullptr;
    };

    template<typename T, typename Promise>
    struct yield_to
    {
        ~yield_to() {
            outln("deactivate yield_to");
        }
        yield_to(Promise* p) : p(p) {
            outln("activate yield_to");
        }

        bool await_ready() noexcept
        {
            out_called("yield_to", "await_ready()", "false");
            return false;
        }

        void await_suspend(ex::resumable_handle<> r) noexcept
        {
            out_from_compiler("yield_to", "await_suspend(resumable_handle<>)");
            p->To = r;
            if (p->From) {
                ex::resumable_handle<> coro{ p->From };
                p->From = nullptr;
                out_call_color("blue", "yield_to", "schedule_periodically", "from()");
                coro();
                out_return_color("green", "yield_to", "schedule_periodically", "void");
            }
            out_to_compiler("yield_to", "void");
        }

        async_iterator<T, Promise> await_resume() noexcept
        {
            out_called("yield_to", "await_resume()", "async_iterator()");
            return{ p };
        }

        Promise* p = nullptr;
    };

    template<typename T, typename Promise>
    struct yield_from
    {
        ~yield_from() {
            outln("deactivate yield_from");
        }
        yield_from(Promise* p) : p(p) {
            outln("activate yield_from");
        }

        bool await_ready() noexcept
        {
            out_called("yield_from", "await_ready()", "false");
            return false;
        }

        void await_suspend(ex::resumable_handle<> r) noexcept
        {
            out_from_compiler("yield_from", "await_suspend(resumable_handle<>)");
            p->From = r;
            if (p->To) {
                ex::resumable_handle<> coro{ p->To };
                p->To = nullptr;
                out_call_color("blue", "yield_from", "async_test", "to()");
                coro();
                out_return_color("green", "yield_from", "async_test", "void");
            }
            out_to_compiler("yield_from", "void");
        }

        void await_resume() noexcept
        {
            out_called("yield_from", "await_resume()", "async_iterator()");
        }

        Promise* p = nullptr;
    };

    template<typename T, typename Alloc = std::allocator<char>>
    struct async_generator
    {
        struct promise_type {
            T* CurrentValue = nullptr;
            bool done = false;
            std::exception_ptr Error;
            ex::resumable_handle<> To{ nullptr };
            ex::resumable_handle<> From{ nullptr };

            ~promise_type() {
                out_called("promise", "~promise()", "void");
                outln("deactivate promise");
            }
            promise_type() {
                outln("activate promise");
            }

            promise_type& get_return_object()
            {
                out_called("promise", "get_return_object()", "*this");
                return *this;
            }

            suspend_always initial_suspend()
            {
                out_called("promise", "initial_suspend()", "suspend_always()");
                return{};
            }

            suspend_always final_suspend()
            {
                out_from_compiler("promise", "final_suspend()");
                if (To) {
                    out_call_color("blue", "promise", "async_test", "to()");
                    To();
                    out_return_color("green", "promise", "async_test", "void");
                }
                out_to_compiler("promise", "suspend_always()");
                return{};
            }

            bool cancellation_requested() const
            {
                out_called("promise", "cancellation_requested()", done ? "true" : "false");
                return done;
            }

            void set_result()
            {
                out_called("promise", "set_result()", "void");
                done = true;
            }

            void set_exception(std::exception_ptr Exc)
            {
                out_called("promise", "set_exception()", "void");
                Error = std::move(Exc);
            }

            yield_from<T, promise_type> yield_value(T Value)
            {
                out_called("promise", "yield_value()", "yield_from()");
                CurrentValue = std::addressof(Value);
                return{ this };
            }
        };

        using iterator = async_iterator<T, promise_type>;
        using value_type = T;

        explicit async_generator(promise_type& Prom)
            : Coro(ex::resumable_handle<promise_type>::from_promise(_STD addressof(Prom)))
        {
            outln("activate async_generator");
        }

        ~async_generator() {
            out_from_compiler("async_generator", "~async_generator()");
            if (Coro)
            {
                out_group_begin("final");
                auto& Prom = Coro.promise();
                if (!Prom.done)
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

                    Prom.done = true;
                    out_call_color("blue", "async_generator", "schedule_periodically", "from()");
                    Coro();
                    out_return_color("green", "async_generator", "schedule_periodically", "void");
                }
                out_call_color("blue", "async_generator", "schedule_periodically", "from()");
                Coro();
                out_return_color("green", "async_generator", "schedule_periodically", "void");
                out_group_end();
            }
            out_to_compiler("async_generator", "void");
            outln("deactivate async_generator");
        }
        async_generator() {
            outln("activate async_generator");
        }

        async_generator(async_generator const& Right)
            : Coro(Right.Coro)
        {
            outln("activate async_generator");
        }

        async_generator& operator = (async_generator const& Right) {
            if (&Right != this)
            {
                Coro = Right.Coro;
            }
        }

        async_generator(async_generator && Right)
            : Coro(Right.Coro)
        {
            Right.Coro = nullptr;
            outln("activate async_generator");
        }

        async_generator& operator = (async_generator && Right)
        {
            if (&Right != this)
            {
                Coro = Right.Coro;
                Right.Coro = nullptr;
            }
        }

        yield_to<T, promise_type> begin() {
            out_call_color("blue", "async_generator", "schedule_periodically", "from()");
            Coro();
            out_return_color("green", "async_generator", "schedule_periodically", "void");
            return{ std::addressof(Coro.promise()) };
        }
        iterator end() {
            return{ nullptr };
        }

    private:
        ex::resumable_handle<promise_type> Coro = nullptr;
    };

    namespace scheduler {
        // usage: await schedule(std::chrono::system_clock::now() + 100ms, [](){. . .});
        template<class Work>
        auto schedule(std::chrono::system_clock::time_point at, Work work) {
            outln("activate schedule");
            class awaiter {
                static void CALLBACK TimerCallback(PTP_CALLBACK_INSTANCE, void* Context, PTP_TIMER) {
                    outln("... one second ...");
                    out_return("awaiter", "os", "TimerCallback()");
                    ex::resumable_handle<>::from_address(Context)();
                }
                PTP_TIMER timer = nullptr;
                std::chrono::system_clock::time_point at;
                Work work;
            public:
                awaiter(std::chrono::system_clock::time_point a, Work w) : at(a), work(std::move(w)) {
                    outln("activate awaiter");
                }
                bool await_ready() const {
                    out_called("awaiter", "await_ready()", !!(std::chrono::system_clock::now() >= at) ? "true" : "false");
                    return std::chrono::system_clock::now() >= at;
                }
                void await_suspend(ex::resumable_handle<> resume_cb) {
                    out_from_compiler("awaiter", "await_suspend(resumable_handle<>)");
                    out_call("awaiter", "os", "CreateThreadpoolTimer()");
                    auto duration = at - std::chrono::system_clock::now();
                    int64_t relative_count = -duration.count();
                    timer = CreateThreadpoolTimer(TimerCallback, resume_cb.to_address(), nullptr);
                    if (timer == 0) throw std::system_error(GetLastError(), std::system_category());
                    SetThreadpoolTimer(timer, (PFILETIME)&relative_count, 0, 0);
                    out_to_compiler("awaiter", "void");
                }
                auto await_resume() {
                    out_called("awaiter", "await_resume()", "work()");
                    return work();
                }
                ~awaiter() {
                    if (timer) CloseThreadpoolTimer(timer);
                    outln("deactivate awaiter");
                }
            };
            outln("deactivate schedule");
            out_return("schedule_periodically", "schedule", "awaiter()");
            return awaiter{ at, work };
        }

        // usage: for await (r : schedule_periodically(std::chrono::system_clock::now(), 100ms, [](int64_t tick){. . .})){. . .}
        template<class Work, typename U = decltype(std::declval<Work>()(0))>
        async::async_generator<U> schedule_periodically(std::chrono::system_clock::time_point initial, std::chrono::system_clock::duration period, Work work) {
            int64_t tick = 0;
            for (;;) {
                out_call("schedule_periodically", "schedule", "await schedule()");
                auto result = __await schedule(initial + (period * tick), [&tick, &work]() {
                    return work(tick);
                });
                out_call("schedule_periodically", "promise", "await yield_value()");
                __yield_value result;
                out_return("schedule_periodically", "promise", "void");
                ++tick;
            }
        }

    }

}
namespace as = async::scheduler;


namespace std {
    namespace experimental {
        template <typename T, typename Alloc, typename... Whatever>
        struct resumable_traits<async::async_generator<T, Alloc>, Whatever...> {
            using allocator_type = Alloc;
            using promise_type = typename async::async_generator<T, Alloc>::promise_type;
        };
    }
}

std::future<void> async_test() {
    outln("activate async_test");
    auto start = clk::now();
    out_call("async_test", "schedule_periodically", "schedule_periodically()");
    out_group_begin("initial");
    outln("activate schedule_periodically");
    auto ticks = as::schedule_periodically(start + 1s, 1s,
        [](int64_t tick) {return tick; });
    out_group_end();
    outln("deactivate schedule_periodically");
    out_return("async_test", "schedule_periodically", "async_generator<int64_t>()");

    out_group_begin("iteration 0");
    out_call("async_test", "async_generator", "await begin()");
    auto cursor = __await ticks.begin();
    out_return("async_test", "async_generator", "async_iterator<int64_t>()");
    auto end = ticks.end();

//    while (cursor != end) {

        out_call("async_test", "async_iterator", "operator*()");
        {auto value = *cursor;}
        out_return("async_test", "async_iterator", "in64_t()");
        out_group_end();

        out_group_begin("iteration 1..N");
        out_call("async_test", "async_iterator", "await operator++()");
        __await ++cursor;
        out_return("async_test", "async_iterator", "async_iterator<int64_t>()");

        out_call("async_test", "async_iterator", "operator*()");
        {auto value = *cursor;}
        out_return("async_test", "async_iterator", "in64_t()");
        out_group_end();
//    }

    outln("deactivate async_test");
}

int wmain() {
    outln("participant compiler");
    outln("participant caller");
    outln("participant async_test");
    outln("participant schedule_periodically");
    outln("participant schedule");
    outln("participant suspend_always");
    outln("participant yield_to");
    outln("participant yield_from");
    outln("participant awaiter");
    outln("participant promise");
    outln("participant future");
    outln("participant async_generator");
    outln("participant async_iterator");
    outln("participant os");

    outln("activate caller");
    try {
        out_call("caller", "async_test", "async_test()");
        auto done = async_test();
        out_group_begin("wait for completion");
        out_return("caller", "async_test", "std::future<void>()");
        out_call("caller", "future", "get()");
        out_group_end();
        done.get();
        out_return("caller", "future", "void");
    }
    catch (const std::exception& e) {
        outln(" exception ", e.what());
    }
    outln("deactivate caller");
}
