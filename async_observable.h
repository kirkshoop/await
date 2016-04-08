#pragma once

namespace async {

    template<class T, class Done>
    async_generator<T> when_done(async_generator<T> from, Done d) {
        try {
            for (auto& v : from) {
                co_yield v;
            }
        } catch(...) {
            d();
            throw;
        }
        d();
    }

    template<class T, class Subscribe = void>
    class async_observable;

    template<class T>
    class async_observable<T, void>
    {
    public:
        using value_type = T;

        template<class Subscribe>
        static auto create(Subscribe s) -> async_observable<T, Subscribe> {
            return async_observable<T, Subscribe>{std::move(s)};
        }

        auto subscribe() const -> async_generator<T> {
            // implementation of an empty sequence
            co_await ex::suspend_never();
        }
    };

    template<class T, class Subscribe>
    class async_observable
    {
        mutable Subscribe s;
    public:
        using value_type = T;

        async_observable(Subscribe s) : s(std::move(s)) {}

        auto subscribe() const -> async_generator<T> {
            return s();
        }

        template<class Lifter,
            class SG = std::result_of_t<Subscribe()>,
            class LG = std::result_of_t<Lifter(SG)>,
            class U = typename LG::value_type>
        auto lift(Lifter l) const {
            auto copy = s;
            return async_observable<U>::create(
                [=]() mutable -> async_generator<U> {
                    return l(copy());
                });
        }
    };
}
