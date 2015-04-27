#pragma once

#include <future>
#include <thread>
#include <queue>

namespace async { namespace operators {

namespace detail {

    template<typename T, typename P>
    async_generator<T> filter(async_generator<T> s, P p) {
        for __await(auto&& v : s) {
            if (p(v)) {
                __yield_value v;
            }
        }
    }

    template<typename T>
    async_generator<T> take(async_generator<T> s, uint64_t remaining) {
        if (!!remaining) {
            for __await(auto&& v : s) {
                __yield_value v;
                if (!--remaining) {
                    break;
                }
            }
        }
    }

    template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))>
    async_generator<U> map(async_generator<T> s, M m) {
        for __await(auto&& v : s) {
            __yield_value m(v);
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
        };

        auto generator_source = [&](async_generator<async_generator<T>> gs) -> std::future<void> {
            for __await(auto&& ns: gs) {
                ++pending;
                auto nsf = source(std::move(ns)).share();
            }
            --pending;
        };

        ++pending;
        auto gsf = generator_source(std::move(s));

        while(pending > 0 || !ch->empty()) {
            auto v = __await ch->pop();
            __yield_value v;
        }
        gsf.get();
    }

    template<typename T, typename Subscriber>
    async_generator<T> merge(async_generator<async_observable<T, Subscriber>> s) {
        auto ch = std::make_unique<merge_channel<T>>();
        int pending = 0;

        auto source = [&](async_observable<T, Subscriber> s) -> std::future<void> {
            for __await(auto&& v: s.subscribe()) {
                ch->push(v);
            }
            --pending;
        };

        auto generator_source = [&](async_generator<async_observable<T, Subscriber>> gs) -> std::future<void> {
            for __await(auto&& ns: gs) {
                ++pending;
                auto nsf = source(std::move(ns)).share();
            }
            --pending;
        };

        ++pending;
        auto gsf = generator_source(std::move(s));

        while(pending > 0 || !ch->empty()) {
            auto v = __await ch->pop();
            __yield_value v;
        }
        gsf.get();
    }

    template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))::value_type>
    async_generator<U> flat_map(async_generator<T> s, M m) {
        return merge(map(std::move(s), m));
    }

    template<typename T>
    async_generator<T> concat(async_generator<async_generator<T>> s) {
        for __await(auto&& ns : s) {
            for __await(auto&& v : ns) {
                __yield_value v;
            }
        }
    }

    template<typename T, typename Subscriber>
    async_generator<T> concat(async_generator<async_observable<T, Subscriber>> s) {
        for __await(auto&& ns : s) {
            for __await(auto&& v : ns.subscribe()) {
                __yield_value v;
            }
        }
    }

    template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))::value_type>
    async_generator<U> concat_map(async_generator<T> s, M m) {
        return concat(map(std::move(s), m));
    }

    template<typename T, typename U>
    async_generator<T> take_until(async_generator<T> s, async_generator<U> e) {

        auto event_done = [&]() mutable -> std::future<void> {
            try {
                for __await(auto&& u : e) {
                    s.cancel();
                    break;
                }
            } catch (...) {
                s.fail(std::current_exception());
            }
        }();

        for __await(auto&& v : s) {
            __yield_value v;
        }
        e.cancel();
        __await event_done;
    }

}

    template<typename P>
    auto filter(P p) {
        return [=](auto&& s) { return async::operators::detail::filter(std::move(s), p); };
    }

    inline auto take(uint64_t count) {
        return [=](auto&& s) { return async::operators::detail::take(std::move(s), count); };
    }

    template<typename M>
    auto map(M m) {
        return [=](auto&& s) { return async::operators::detail::map(std::move(s), m); };
    }

    inline auto merge() {
        return [=](auto&& s) { return async::operators::detail::merge(std::move(s)); };
    }

    template<typename M>
    auto flat_map(M m) {
        return [=](auto&& s) { return async::operators::detail::flat_map(std::move(s), m); };
    }

    inline auto concat() {
        return [=](auto&& s) { return async::operators::detail::concat(std::move(s)); };
    }

    template<typename M>
    auto concat_map(M m) {
        return [=](auto&& s) { return async::operators::detail::concat_map(std::move(s), m); };
    }

    template<typename U>
    auto take_until(async_generator<U> e) {
        return [=](auto&& s) { return async::operators::detail::take_until(std::move(s), e); };
    }
    template<typename U, typename Subscriber>
    auto take_until(async_observable<U, Subscriber> e) {
        return [=](auto&& s) { return async::operators::detail::take_until(std::move(s), e.subscribe()); };
    }

} }
