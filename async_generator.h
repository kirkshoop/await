#pragma once

#include <functional>
#include <atomic>
#include <mutex>
#include <iomanip>

static std::atomic<int> id_source;
static std::mutex outlock;
struct record_lifetime
{
    std::string label;
    int id;
    record_lifetime(std::string l) : label(l), id(++id_source) {(*this)(" enter");}
    ~record_lifetime() {(*this)(" exit");}
    template<class... T>
    void operator()(T... t) const {
#if 0
        std::unique_lock<std::mutex> guard(outlock);
        std::cout << std::this_thread::get_id() << " - " << label << " - " << id;
        int seq[] = {(std::cout << t, 0)...};
        std::cout << std::endl;
#endif
    }
};

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
            return p == Right.p || (!Right.p && p && p->canceled) || (!p && Right.p && Right.p->canceled);
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

    struct promise_cancelation
    {
        record_lifetime scope{" promise_type"};
        mutable std::function<void()> oncancel;
        mutable bool canceled = false;
        mutable ex::resumable_handle<> From{ nullptr };
        mutable ex::resumable_handle<> To{ nullptr };
#if 1
        mutable promise_cancelation* CancelFrom = nullptr;
        mutable promise_cancelation* CancelTo = nullptr;
        void set_cancelation(promise_cancelation* o) {
            // connect to other async_generator
            scope(" set_cancelation from ", o->scope.id, " - ", std::boolalpha, canceled, !!o->CancelFrom, !!CancelTo);
            if (!o->CancelFrom) {
                o->CancelFrom = this;
            }
            if (!CancelTo) {
                CancelTo = o;
            }
        }
        void set_cancelation(...) {
            // unrecognized promise. Nothing to do
        }
        void cancel_from() const {
            scope(" cancel_from");
            canceled = true;
            if (CancelFrom) {CancelFrom->cancel_from();}
        }
        void cancel_to() const {
            scope(" cancel_to");
            canceled = true;
            if (CancelTo) {CancelTo->cancel_to();}
        }
#endif
        void attach(std::function<void()> oc) {
            scope(" attach");
            oncancel = std::move(oc);
        }
        void notify_from() const {
            scope(" notify_from");
            if (CancelFrom) {CancelFrom->notify_from();}
            if (oncancel) {
                scope(" notify_from oncancel");
                auto oc = oncancel; oncancel = nullptr; oc();}
            if (From) {
                auto from = From;
                From = nullptr;
                scope(" ~ calling from");
                from();
                scope(" ~ resumed from");
            }
        }
        void notify_to() const {
            scope(" notify_to");
            if (oncancel) {
                scope(" notify_to oncancel");
                auto oc = oncancel; oncancel = nullptr; oc();}
            if (CancelTo) {CancelTo->notify_to();}
#if 0
            if (To) {
                auto to = To;
                To = nullptr;
                scope(" ~ calling to");
                to();
                scope(" ~ resumed to");
            }
#endif
        }
        void cancel() {
            scope(" cancel");
            canceled = true;
            cancel_from();
            cancel_to();
            notify_from();
            notify_to();
        }
    };

    struct attach_oncancel
    {
        attach_oncancel(std::function<void()> oc) : oncancel(oc) {}

        bool await_ready() noexcept
        {
            return false;
        }

        template<class Handle>
        void await_suspend(Handle r) noexcept
        {
            r.promise().attach(oncancel);
            r();
        }

        void await_resume() noexcept
        {
        }

        std::function<void()> oncancel;
    };

    template<typename T, typename Promise>
    struct yield_to
    {
        yield_to(Promise* p) : p(p) {}

        bool await_ready() noexcept
        {
            return false;
        }

        template<class Handle>
        void await_suspend(Handle r) noexcept
        {
            p->set_cancelation(std::addressof(r.promise()));
            if (p->canceled) {
                r();
                return;
            }
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
            if (p->canceled) {
                r();
                return;
            }
            p->From = r;
            if (!p->canceled && p->To) {
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
        struct promise_type : public promise_cancelation {
            T* CurrentValue = nullptr;
            std::exception_ptr Error;

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
                scope(" final_suspend", std::boolalpha, canceled, !!From, !!To);
                if (To) {
                    auto to = To;
                    To = nullptr;
                    scope(" ~ calling to");
                    to();
                    scope(" ~ resumed to");
                }
                return{};
            }

            bool cancellation_requested() const
            {
                scope(" cancellation_requested", " -> ", std::boolalpha, canceled);
                return canceled;
            }

            void set_result()
            {
                cancel();
                scope(" set_result enter", " -> ", std::boolalpha, canceled, !!From, !!To);
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
            cancel();
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

        void attach(std::function<void()> oncancel) {
            Coro.promise().attach(std::move(oncancel));
        }

        void cancel() {
            if (Coro)
            {
                auto coro = Coro;
                Coro = nullptr;

                auto& Prom = coro.promise();

                Prom.scope(" ~ ", std::boolalpha, Prom.canceled, !!Prom.From, !!Prom.To);

                if (!Prom.canceled) {
                    Prom.cancel();
                }
            }
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

