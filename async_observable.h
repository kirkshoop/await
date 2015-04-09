#pragma once

namespace async {

    template<class T, class Done>
    async_generator<T> when_done(async_generator<T> from, Done d) {
        try {
            for (auto& v : from) {
                __yield_value v;
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
            __await ex::suspend_never();
        }
    };

    template<class T, class Subscribe>
    class async_observable
    {
        mutable std::string id;
        mutable Subscribe s;
    public:
        using value_type = T;

        explicit async_observable(Subscribe s) : s(std::move(s)) {}

        async_observable& set_id(std::string theid) {
            this->id = theid;
            return *this;
        }

        auto subscribe() const -> async_generator<T> {
            return s();
        }

        template<class Lifter,
            class SG = std::result_of_t<Subscribe()>,
            class LG = std::result_of_t<Lifter(SG)>,
            class U = typename LG::value_type>
        auto lift(Lifter l) const {
            auto copy = s;
            auto theid = id;
            return async_observable<U>::create(
                [=]() mutable -> async_generator<U> {
                    return l(copy().set_id(theid));
                });
        }
    };
}
