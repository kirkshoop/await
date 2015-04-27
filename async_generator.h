#pragma once

#include <functional>

namespace async {
    namespace ex = std::experimental;

    struct GeneratorStateBase : public std::enable_shared_from_this<GeneratorStateBase>
    {
        bool done = false;
        bool cancel_guard = false;
        std::exception_ptr Error{ nullptr };
        ex::resumable_handle<> To{ nullptr };
        ex::resumable_handle<> From{ nullptr };
        ex::resumable_handle<> Final{ nullptr };
        std::weak_ptr<GeneratorStateBase> pto;
        std::weak_ptr<GeneratorStateBase> pfrom;
        std::function<void()> onbegin;
        std::function<void()> oncancel;

#ifdef _DEBUG
        std::string id{};
        std::string intent{};

        inline void set_id(std::string&& ida) {id = std::move(ida);}
        inline void set_intent(std::string&& intenta) {intent = std::move(intenta);}

        inline void do_write_state() {
            printf("  [%30s - %30s; done=%d, cancel=%d, error=%d, to=%d, from=%d, final=%d]\n", 
                id.c_str(), intent.c_str(), 
                !!done, !!cancel_guard, !!Error, 
                !!To, !!From, !!Final);
            auto from = pfrom.lock();
            if (from) {
                from->do_write_state();
            }
        }
        inline void write_state_from_start() {
            auto to = pto.lock();
            if (to) {
                to->write_state_from_start();
            } else {
                do_write_state();
            }
        }
        inline void write_state() {
            printf("\n>> %30s - %30s\n", id.c_str(), intent.c_str());
            write_state_from_start();
            printf("<< %30s - %30s\n", id.c_str(), intent.c_str());
        }
#else
        inline void set_id(std::string&&) {}
        inline void set_intent(std::string&&) {}

        inline void write_state() {}
#endif

        ~GeneratorStateBase() {
            set_intent("~GeneratorStateBase");
            write_state();
        }

        void call_from(std::string theintent){
            if (From) {
                auto local = this->shared_from_this();
                local->set_intent("from " + theintent);
                write_state();
                auto from = std::move(From);
                From = nullptr;
                from();
                local->set_intent("from -" + theintent);
                write_state();
            }
        }

        void call_to(std::string theintent){
            if (To) {
                auto local = this->shared_from_this();
                local->set_intent("to " + theintent);
                write_state();
                auto to = std::move(To);
                To = nullptr;
                to();
                local->set_intent("to -" + theintent);
                write_state();
            }
        }

        void call_final(std::string theintent){
            if (Final) {
                done = true;
                auto local = this->shared_from_this();
                local->set_intent("final " + theintent);
                write_state();
                auto thefinal = std::move(Final);
                Final = nullptr;
                thefinal();
                local->set_intent("final -" + theintent);
                write_state();
            }
        }

        static bool same(GeneratorStateBase* l, GeneratorStateBase* r) {return l == r;}

        template<class Promise, class Ptr = decltype(std::declval<Promise>().get())>
        void set_from(Promise& p){
            auto local = this->shared_from_this();
            if (same(p.get().get(), local.get())) {
                set_intent("yield produced value");
                write_state();
                return;
            }
            set_intent("yield piped value");
            write_state();
        }

        template<class Promise, class Ptr = decltype(std::declval<Promise>().get())>
        void set_to(Promise& p){
            auto local = this->shared_from_this();
            pto = p.get();
            p.get()->pfrom = local;
            set_intent("await iterator");
            write_state();
        }
        void set_to(...){
            auto local = this->shared_from_this();
            local->set_intent("await iterator from generator");
            write_state();
        }

        void cancel(std::string theintent){
            auto local = this->shared_from_this();
            local->set_intent(theintent + " cancel");
            write_state();
            if (cancel_guard || (!To && !From)) {return;}

            cancel_guard = true;
            Final = std::move(To);
            To = nullptr;

            if (oncancel) {
                auto down = std::move(oncancel);
                oncancel = nullptr;
                down();
            }
            call_from(theintent + " cancel");
            auto from = pfrom.lock();
            if (from) {
                from->cancel(theintent);
            }
        }

        void fail(std::exception_ptr ep, std::string theintent) {
            auto local = this->shared_from_this();
            local->set_intent(theintent + " fail");
            write_state();
            if (cancel_guard || (!To && !From)) {return;}

            cancel_guard = true;
            local->Error = ep;
            call_to(theintent + " fail");

            if (oncancel) {
                auto down = std::move(oncancel);
                oncancel = nullptr;
                down();
            }
            auto from = pfrom.lock();
            if (from) {
                from->cancel(theintent);
            }
        }
    };

    template<class T>
    struct GeneratorState : public GeneratorStateBase
    {
        std::shared_ptr<T> CurrentValue = nullptr;
    };

    template<class T>
    using GeneratorStatePtr = std::shared_ptr<GeneratorState<T>>;

    namespace detail {
        template<class F>
        struct oncancel_awaiter
        {
            F f;

            bool await_ready() {
                return false;
            }

            template<class Promise>
            void await_suspend(ex::resumable_handle<Promise> r) {
                r.promise().p->oncancel = std::move(f);
                r();
            }

            void await_resume() {
            }
        };
    }

    template<class F>
    auto add_oncancel(F&& f)
    {
        return detail::oncancel_awaiter<F>{std::forward<F>(f)};
    };

    namespace detail {
        template<class F>
        struct onbegin_awaiter
        {
            F f;

            bool await_ready() {
                return false;
            }

            template<class Promise>
            void await_suspend(ex::resumable_handle<Promise> r) {
                r.promise().p->onbegin = std::move(f);
                r();
            }

            void await_resume() {
            }
        };
    }

    template<class F>
    auto add_onbegin(F&& f)
    {
        return detail::onbegin_awaiter<F>{std::forward<F>(f)};
    };

    template<typename T>
    struct yield_to;

    template<typename T>
    struct async_iterator : public std::iterator<std::input_iterator_tag, T>
    {
        async_iterator(GeneratorStatePtr<T> p) : p(p) {
        }

        yield_to<T> operator++()
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

        GeneratorStatePtr<T> p = nullptr;
    };

    template<typename T>
    struct yield_to
    {
        yield_to(GeneratorStatePtr<T> p) : p(p) {}

        bool await_ready() noexcept
        {
            return false;
        }

        template<class Promise>
        void await_suspend(ex::resumable_handle<Promise> r) noexcept
        {
            auto local = p;
            local->To = r;
            local->set_to(r.promise());
            local->call_from("produce value");
            if (local->onbegin) {
                auto up = std::move(local->onbegin);
                local->onbegin = nullptr;
                up();
            }
        }

        async_iterator<T> await_resume() noexcept
        {
            return{ p };
        }

        GeneratorStatePtr<T> p = nullptr;
    };

    template<typename T>
    struct yield_from
    {
        yield_from(GeneratorStatePtr<T> p) : p(p) {}

        auto& yield_wait() {
            return wait;
        }

        bool await_ready() noexcept
        {
            return false;
        }

        template<class Promise>
        void await_suspend(ex::resumable_handle<Promise> r) noexcept
        {
            auto local = p;
            local->From = r;
            local->set_from(r.promise());
            local->call_to("consume value");
        }

        void await_resume() noexcept
        {
        }

        GeneratorStatePtr<T> p = nullptr;
    };

    template<typename T, typename Alloc = std::allocator<char>>
    struct async_generator
    {
        struct promise_type {
            GeneratorStatePtr<T> p;

            ~promise_type() {
            }
            promise_type() : p(std::make_shared<GeneratorState<T>>()) {
            }

            promise_type& get_return_object()
            {
                return *this;
            }

            ex::suspend_never initial_suspend()
            {
                return{};
            }

            ex::suspend_never final_suspend()
            {
                auto local = p;
                local->call_final("final consumer resume");
                return{};
            }

            bool cancellation_requested() const
            {
                return p->done;
            }

            void set_result()
            {
                p->cancel("set_result");
            }

            void set_exception(std::exception_ptr Exc)
            {
                p->Error = std::move(Exc);
            }

            yield_from<T> yield_value(T Value)
            {
                if (!p->CurrentValue) {
                    p->CurrentValue = std::make_shared<T>(std::move(Value));
                } else {
                    // use construction to allow types that are not assignable
                    (*p->CurrentValue).~T();
                    new(p->CurrentValue.get()) T(std::move(Value));
                }
                return{ p };
            }

            GeneratorStatePtr<T> get() {
                return p;
            }
        };

        using iterator = async_iterator<T>;
        using value_type = T;

        explicit async_generator(promise_type& Prom)
            : p(Prom.p)
            , Coro(ex::resumable_handle<promise_type>::from_promise(_STD addressof(Prom)))
        {
        }

        ~async_generator() {
            if (!!p) {
                auto local = p;
                p = nullptr;
                local->cancel("~async_generator");
            }
        }

        async_generator(async_generator const& Right)
            : p(Right.p)
            , Coro(Right.Coro)
        {
        }

        async_generator& operator = (async_generator const& Right) {
            if (&Right != this)
            {
                p = Right.p;
                Coro = Right.Coro;
            }
        }

        async_generator(async_generator && Right)
            : p(Right.p)
            , Coro(Right.Coro)
        {
            Right.p = nullptr;
            Right.Coro = nullptr;
        }

        async_generator& operator = (async_generator && Right)
        {
            if (&Right != this)
            {
                p = Right.p;
                Coro = Right.Coro;
                Right.p = nullptr;
                Right.Coro = nullptr;
            }
        }

        async_generator set_id(std::string id){
            p->set_id(std::move(id));
            return *this;
        }

        yield_to<T> begin() {
            return{ p };
        }
        iterator end() {
            return{ nullptr };
        }

        void cancel() {
            if (!!p) {
                auto local = p;
                p = nullptr;
                local->cancel("async_generator::");
            }
        }

        void fail(std::exception_ptr ep) {
            if (!!p) {
                auto local = p;
                p = nullptr;
                local->fail(ep, "async_generator::");
            }
        }

    private:
        GeneratorStatePtr<T> p;
        ex::resumable_handle<promise_type> Coro = nullptr;
    };

    template<typename T, typename F>
    auto operator|(async_generator<T> t, F f) {
        return f(std::move(t));
    }
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

