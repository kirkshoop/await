#pragma once

namespace lazy { namespace operators {

    namespace detail {

        template<typename T, typename P>
        ex::generator<T> filter(ex::generator<T> s, P p) {
            for (auto&& v : s) {
                if (p(v)) {
                    __yield_value v;
                }
            }
        }

        template<typename T>
        ex::generator<T> take(ex::generator<T> s, uint64_t remaining) {
            for (auto&& v : s) {
                if (!remaining--) {
                    break;
                }
                __yield_value v;
            }
        }

        template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()))>
        ex::generator<U> map(ex::generator<T> s, M m) {
            for (auto&& v : s) {
                __yield_value m(v);
            }
        }

        template<typename T>
        ex::generator<T> concat(ex::generator<ex::generator<T>> s) {
            auto scursor = s.begin();
            auto send = s.end();
            for (;scursor != send; ++scursor) {
                auto vcursor = const_cast<ex::generator<T>&>(*scursor).begin();
                auto vend = const_cast<ex::generator<T>&>(*scursor).end();
                for (; vcursor != vend; ++vcursor) {
                    __yield_value *vcursor;
                }
            }
        }

        template<typename T, typename M, typename U = decltype(std::declval<M>()(std::declval<T>()).end())::value_type>
        ex::generator<U> concat_map(ex::generator<T> s, M m) {
            return concat(map(std::move(s), m));
        }
    }

    template<typename P>
    auto filter(P p) {
        return [=](auto&& s) { return lazy::operators::detail::filter(std::move(s), p); };
    }

    auto take(uint64_t count) {
        return [=](auto&& s) { return lazy::operators::detail::take(std::move(s), count); };
    }

    template<typename M>
    auto map(M m) {
        return [=](auto&& s) { return lazy::operators::detail::map(std::move(s), m); };
    }

    auto merge() {
        return [=](auto&& s) { return lazy::operators::detail::merge(std::move(s)); };
    }

    template<typename M>
    auto concat_map(M m) {
        return [=](auto&& s) { return lazy::operators::detail::concat_map(std::move(s), m); };
    }

    template<typename T, typename F>
    auto operator|(ex::generator<T>&& t, F f) {
        return f(t);
    }
} }
