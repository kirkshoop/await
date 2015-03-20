#pragma once

namespace async { namespace subjects {

    namespace detail {
        struct subscription : public std::enable_shared_from_this<subscription>
        {
            ex::resumable_handle<> Resume;

            struct awaiter
            {
                subscription* s;

                bool await_ready() {
                    return false;
                }

                void await_suspend(ex::resumable_handle<> rh) {
                    s->Resume = rh;
                }

                void await_resume() {
                }
            };
            awaiter next() {
                return awaiter{this};
            }

        };
    }

    template<class T>
    struct subject {
        bool Done = false;
        std::exception_ptr Error;
        T CurrentValue;

        std::vector<std::shared_ptr<detail::subscription>> subscriptions;

        async::async_generator<T> subscribe() {
            auto s = std::make_shared<detail::subscription>();
            subscriptions.push_back(s);
            for (;;) {
                __await s->next();
                if (Done) {
                    break;
                }
                if (Error) {
                    std::rethrow_exception(Error);
                }
                __yield_value CurrentValue;
            }
        }

        void next(T t) {
            if (Done || Error) {return;}
            CurrentValue = t;
            pump();
        }
        void error(std::exception_ptr ep) {
            if (Done || Error) {return;}
            Error = ep;
            pump();
        }
        void complete() {
            if (Done || Error) {return;}
            Done = true;
            pump();
        }

    private:
        void pump() {
            for (auto& v : subscriptions) {
                if (v->Resume) {
                    ex::resumable_handle<> resume{nullptr};
                    using std::swap;
                    swap(resume, v->Resume);
                    resume();
                }
            }
        }
    };

} }
