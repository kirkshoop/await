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
        for __await(auto&& v : s) {
            if (p(v)) {
                scope(" yield");
                __yield_value v;
            }
            scope(" await");
        }
    }

    template<typename T>
    async_generator<T> take(async_generator<T> s, uint64_t remaining) {
        record_lifetime scope("take");
        if (!!remaining) {
            scope(" await");
            for __await(auto&& v : s) {
                scope(" yield");
                __yield_value v;
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
        for __await(auto&& v : s) {
            scope(" yield");
            __yield_value m(v);
            scope(" await");
        }
    }

    template<typename T>
    struct merge_channel
    {
        std::queue<T> queue;
        mutable std::recursive_mutex lock;
        ex::resumable_handle<> coro;

        merge_channel() : coro(nullptr) {}

        struct awaitable {
            awaitable(merge_channel* t) : that(t) {}

            bool await_ready() const {
                std::unique_lock<std::recursive_mutex> guard(that->lock);
                return !that->queue.empty();
            }
            void await_suspend(ex::resumable_handle<> c) {
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

        awaitable pop(){
            return {this};
        }

        void push(const T& v) {
            std::unique_lock<std::recursive_mutex> guard(lock);
            queue.push(v);
            guard.unlock();
            if(coro){
                ex::resumable_handle<> c = coro;
                coro = nullptr;
                c();
            }
        }
    };

    template<typename T>
    async_generator<T> merge(async_generator<T> lhs, async_generator<T> rhs) {
        auto ch = std::make_unique<merge_channel<T>>();
        int pending = 2;

        auto source = [&](async_generator<T> s) -> std::future<void> {
            for __await(auto&& v: s) {
                ch->push(v);
            }
            --pending;
        };

        auto lf = source(std::move(lhs));
        auto rf = source(std::move(rhs));

        while(pending > 0 && !ch->empty()) {
            auto v = __await ch->pop();
            __yield_value v;
        }
    }

    template<typename T>
    async_generator<T> merge(async_generator<async_generator<T>> s) {
        auto ch = std::make_unique<merge_channel<T>>();
        int pending = 0;

        auto source = [&](async_generator<T> s) -> std::future<void> {
            for __await(auto&& v: s) {
                ch->push(v);
            }
            --pending;
std::cout << "exit merge source" << std::endl;
        };

        auto generator_source = [&](async_generator<async_generator<T>> gs) -> std::future<void> {
            for __await(auto&& ns: gs) {
                ++pending;
                auto nsf = source(std::move(ns)).share();
            }
            --pending;
std::cout << "exit merge gen source" << std::endl;
        };

        ++pending;
        auto gsf = generator_source(std::move(s));

        while(pending > 0 || !ch->empty()) {
            auto v = __await ch->pop();
            __yield_value v;
        }
        gsf.get();
std::cout << "exit merge" << std::endl;
    }

    template<typename T, typename Subscriber>
    async_generator<T> merge(async_generator<async_observable<T, Subscriber>> s) {
        auto ch = std::make_unique<merge_channel<T>>();
        int pending = 0;
        std::queue<async_generator<T>*> subscriptions;
        std::queue<std::shared_future<void>> pending_sources;

        record_lifetime scope("merge");

        __await async::attach_oncancel{
            [&](){
                s.cancel();
                scope(" gen source canceled");
                while (!subscriptions.empty()){
                    scope(" cancel nested");
                    subscriptions.front()->cancel();
                    subscriptions.pop();
                }
            }
        };

        auto source = [&](async_observable<T, Subscriber> s) -> std::future<void> {
            auto g = s.subscribe();
            subscriptions.push(std::addressof(g));
            for __await(auto&& v: g) {
                ch->push(v);
            }
            --pending;
scope(" exit source");
        };

        auto generator_source = [&](async_generator<async_observable<T, Subscriber>> gs) -> std::future<void> {
            for __await(auto&& ns: gs) {
                ++pending;
                auto nsf = source(std::move(ns)).share();
                pending_sources.push(nsf);
            }
            --pending;
scope(" exit gen source");
        };

        ++pending;
        auto gsf = generator_source(std::move(s));

        while(pending > 0 || !ch->empty()) {
            auto v = __await ch->pop();
            __yield_value v;
        }
        gsf.get();
        while (!pending_sources.empty()){
            pending_sources.front().get();
            pending_sources.pop();
        }
    }

    template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))::value_type>
    async_generator<U> flat_map(async_generator<T> s, M m) {
        return merge(map(std::move(s), m));
    }

    template<typename T, typename U>
    async_generator<T> take_until(async_generator<T> s, async_generator<U> e) {
        std::atomic_bool Done{false};
        s.attach([&](){
            e.cancel();
        });
        e.attach([&](){
            s.cancel();
        });
        [&]() -> std::future<void> {
            for __await(auto&& u : e) {
                break;
            }
            Done = true;
        }();
        __await s.cancelation_token();
        for __await(auto&& v : s) {
            if (Done) {
                break;
            }
            __yield_value v;
        }
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
