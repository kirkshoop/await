#pragma once

#include <future>
#include <thread>
#include <queue>

namespace async { namespace operators {

namespace detail {

    template<typename T, typename P>
    async_generator<T> filter(async_generator<T> s, P p) {
        record_lifetime scope("filter");
        scope(" await");
        for co_await(auto&& v : s) {
            if (p(v)) {
                scope(" yield");
                co_yield v;
            }
            scope(" await");
        }
    }

    template<typename T>
    async_generator<T> take(async_generator<T> s, uint64_t remaining) {
        record_lifetime scope("take");
        if (!!remaining) {
            scope(" await");
            for co_await(auto&& v : s) {
                scope(" yield");
                co_yield v;
                if (1 == remaining--) {
                    scope(" break");
                    break;
                }
                scope(" await");
            }
        }
    }

    template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))>
    async_generator<U> map(async_generator<T> s, M m) {
        record_lifetime scope("map");
        scope(" await");
        for co_await(auto&& v : s) {
            scope(" yield");
            co_yield m(v);
            scope(" await");
        }
    }

    template<typename T>
    struct merge_channel
    {
        std::queue<T> queue;
        mutable std::recursive_mutex lock;
        std::atomic<bool> canceled;
        std::atomic<int> pending;
        ex::coroutine_handle<> coro;
        std::queue<async_generator<T>*> subscriptions;
        std::queue<std::shared_future<void>> pending_sources;

        ~merge_channel(){
#if 0
            std::unique_lock<std::recursive_mutex> guard(lock);
            while (!pending_sources.empty()){
                pending_sources.front().get();
                pending_sources.pop();
            }
#endif
        }
        merge_channel() : coro(nullptr), canceled(false), pending(0) {}

        struct awaitable {
            explicit awaitable(merge_channel* t) : that(t) {}

            bool await_ready() const {
                std::unique_lock<std::recursive_mutex> guard(that->lock);
                return !that->queue.empty();
            }
            void await_suspend(ex::coroutine_handle<> c) {
                that->coro = c;
            }
            T await_resume() {
                std::unique_lock<std::recursive_mutex> guard(that->lock);
                auto v = that->queue.front();
                that->queue.pop();
                guard.unlock();
                return v;
            }
            merge_channel* that = nullptr;
        };

        bool empty() {
            std::unique_lock<std::recursive_mutex> guard(lock);
            return queue.empty();
        }

        auto pop(){
            return awaitable{this};
        }

        void push(const T& v) {
            std::unique_lock<std::recursive_mutex> guard(lock);
            queue.push(v);
            guard.unlock();
            if(coro){
                ex::coroutine_handle<> c = coro;
                coro = nullptr;
                c();
            }
        }

        void add(async_generator<T>& g) {
            std::unique_lock<std::recursive_mutex> guard(lock);
            subscriptions.push(std::addressof(g));
        }

        void add(std::shared_future<void> nsf) {
            std::unique_lock<std::recursive_mutex> guard(lock);
            pending_sources.push(nsf);
        }

        void cancel() {
            std::unique_lock<std::recursive_mutex> guard(lock);
            canceled = true;
            while (!subscriptions.empty()){
                guard.unlock();
                subscriptions.front()->cancel();
                guard.lock();
                subscriptions.pop();
            }
        }

        struct finished_t {
            explicit finished_t(merge_channel* t) : that(t) {}

            bool await_ready() const {
                return false;
            }
            void await_suspend(ex::coroutine_handle<> c) {
            }
            bool await_resume() {
                std::unique_lock<std::recursive_mutex> guard(that->lock);
                while (!that->pending_sources.empty() &&
                    that->pending_sources.front().wait_for(0s) != std::future_status::deferred){
                    that->pending_sources.pop();
                }
                return that->pending_sources.empty();
            }
            merge_channel* that;
        };

        auto finished(){
            return finished_t{this};
        }
    };

    template<typename T>
    async_generator<T> merge(async_generator<T> lhs, async_generator<T> rhs) {
        auto ch = std::make_unique<merge_channel<T>>();
        int pending = 2;

        auto source = [&](async_generator<T> s) -> std::future<void> {
            for co_await(auto&& v: s) {
                ch->push(v);
            }
            --pending;
        };

        auto lf = source(std::move(lhs));
        auto rf = source(std::move(rhs));

        while(pending > 0 && !ch->empty()) {
            auto v = co_await ch->pop();
            co_yield v;
        }
    }

    template<typename T>
    async_generator<T> merge(async_generator<async_generator<T>> s) {
        auto ch = std::make_unique<merge_channel<T>>();

        record_lifetime scope("merge gen");
        auto& cancelation = co_await async::cancelation_ref{};

        auto source = [&](async_generator<T> g) -> std::future<void> {
            if (!ch->canceled) {
                g.add(cancelation);
                ch->add(g);
                for co_await(auto&& v: g) {
                    ch->push(v);
                }
            }
            --ch->pending;
            scope(" exit source");
        };

        auto generator_source = [&](async_generator<async_generator<T>> gs) -> std::future<void> {
            gs.add(cancelation);
            for co_await(auto&& ns: gs) {
                if (ch->canceled) {break;}
                ++ch->pending;
                auto nsf = source(std::move(ns)).share();
                ch->add(nsf);
                if (ch->canceled) {break;}
            }
            --ch->pending;
            scope(" exit gen source");
        };

        ++ch->pending;
        auto gsf = generator_source(std::move(s)).share();
        ch->add(gsf);

        while(ch->pending > 0 || !ch->empty()) {
            auto v = co_await ch->pop();
            co_yield v;
        }

        scope(" wait for nested to finish...");

        while (!co_await ch->finished()){
            scope(" nested not finished...");
        }

        scope("all nested finished");

    }

    template<typename T, typename Subscriber>
    async_generator<T> merge(async_generator<async_observable<T, Subscriber>> s) {
        auto ch = std::make_unique<merge_channel<T>>();

        record_lifetime scope("merge of lazy");
        auto& cancelation = co_await async::cancelation_ref{};

        auto source = [&](async_observable<T, Subscriber> s) -> std::future<void> {
            if (!ch->canceled) {
                auto g = s.subscribe();
                g.add(cancelation);
                ch->add(g);
                for co_await(auto&& v: g) {
                    ch->push(v);
                }
            }
            --ch->pending;
scope(" exit source");
        };

        auto generator_source = [&](async_generator<async_observable<T, Subscriber>> gs) -> std::future<void> {
            gs.add(cancelation);
            for co_await(auto&& ns: gs) {
                if (ch->canceled) {break;}
                ++ch->pending;
                auto nsf = source(std::move(ns)).share();
                ch->add(nsf);
                if (ch->canceled) {break;}
            }
            --ch->pending;
scope(" exit gen source");
        };

        ++ch->pending;
        auto gsf = generator_source(std::move(s)).share();
        ch->add(gsf);

        while(ch->pending > 0 || !ch->empty()) {
            auto v = co_await ch->pop();
            co_yield v;
        }

        scope(" wait for nested to finish...");

        while (!co_await ch->finished()){
            scope(" nested not finished...");
        }

        scope("all nested finished");
    }

    template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))::value_type>
    async_generator<U> flat_map(async_generator<T> s, M m) {
        return merge(map(std::move(s), m));
    }

    template<typename T, typename U>
    async_generator<T> take_until(async_generator<T> s, async_generator<U> e) {
        std::atomic_bool Done{false};

        auto& cancelation = co_await async::cancelation_ref{};

        auto event_complete = [&]() -> std::future<void> {
            e.add(cancelation);
            for co_await(auto&& u : e) {
                break;
            }
            Done = true;
        }();

        co_await s.cancelation_token();
        for co_await(auto&& v : s) {
            if (Done) {
                break;
            }
            co_yield v;
        }

        event_complete.get();
    }

}

    template<typename P>
    auto filter(P p) {
        return [=](auto&& s) { return async::operators::detail::filter(std::move(s), p); };
    }

    auto take(uint64_t count) {
        return [=](auto&& s) { return async::operators::detail::take(std::move(s), count); };
    }

    template<typename M>
    auto map(M m) {
        return [=](auto&& s) { return async::operators::detail::map(std::move(s), m); };
    }

    auto merge() {
        return [=](auto&& s) { return async::operators::detail::merge(std::move(s)); };
    }

    template<typename M>
    auto flat_map(M m) {
        return [=](auto&& s) { return async::operators::detail::flat_map(std::move(s), m); };
    }

    template<typename U>
    auto take_until(async_generator<U> e) {
        return [=](auto&& s) { return async::operators::detail::take_until(std::move(s), e); };
    }

    template<typename T, typename F>
    auto operator|(async_generator<T> t, F f) {
        return f(std::move(t));
    }

    template<typename T, typename Subscriber, typename Lifter>
    auto operator|(async_observable<T, Subscriber> t, Lifter l) {
        return t.lift(std::move(l));
    }
} }
