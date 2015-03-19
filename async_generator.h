#pragma once

namespace async {
    namespace ex = std::experimental;

    template<typename T, typename Promise>
    struct yield_to;

    template<typename T, typename Promise>
    struct async_iterator : public std::iterator<std::input_iterator_tag, T>
    {
        async_iterator(Promise* p) : p(p) {
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
        yield_to(Promise* p) : p(p) {}

        bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(ex::resumable_handle<> r) noexcept
        {
            p->To = r;
            if (p->From) {
                ex::resumable_handle<> coro{ p->From };
                p->From = nullptr;
                coro();
            }
        }

        async_iterator<T, Promise> await_resume() noexcept
        {
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
            return false;
        }

        void await_suspend(ex::resumable_handle<> r) noexcept
        {
            p->From = r;
            if (p->To) {
                ex::resumable_handle<> coro{ p->To };
                p->To = nullptr;
                coro();
            }
        }

        void await_resume() noexcept
        {
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
            }
            promise_type() {
            }

            promise_type& get_return_object()
            {
                return *this;
            }

            ex::suspend_always initial_suspend()
            {
                return{};
            }

            ex::suspend_always final_suspend()
            {
                if (To) {
                    To();
                }
                return{};
            }

            bool cancellation_requested() const
            {
                return done;
            }

            void set_result()
            {
                done = true;
            }

            void set_exception(std::exception_ptr Exc)
            {
                Error = std::move(Exc);
            }

            yield_from<T, promise_type> yield_value(T Value)
            {
                CurrentValue = std::addressof(Value);
                return{ this };
            }
        };

        using iterator = async_iterator<T, promise_type>;
        using value_type = T;

        explicit async_generator(promise_type& Prom)
            : Coro(ex::resumable_handle<promise_type>::from_promise(_STD addressof(Prom)))
        {
        }

        ~async_generator() {
            if (Coro)
            {
                auto& Prom = Coro.promise();
                if (Prom.From)
                {
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
                        Prom.From();
                    }
                    Prom.From();
                }
            }
        }
        async_generator() {
        }

        async_generator(async_generator const& Right)
            : Coro(Right.Coro)
        {
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
            Coro();
            return{ std::addressof(Coro.promise()) };
        }
        iterator end() {
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

