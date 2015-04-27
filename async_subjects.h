#pragma once

namespace async { namespace subjects {

    namespace detail {

        template<class... T>
        void print(T&&... t) {
#if 0
            printf(std::forward<T>(t)...);
#else
            bool d[] = {(t, false)...};
#endif
        }

        struct subscription : public std::enable_shared_from_this<subscription>
        {
            std::string id;
            ex::resumable_handle<> Resume {nullptr};

            subscription(std::string id) : id(id) {}

            static std::atomic<int64_t>& pend(){
                static std::atomic<int64_t> pending{0};
                return pending;
            }

            struct awaiter
            {
                subscription* s;

                bool await_ready() {
                    return false;
                }

                void await_suspend(ex::resumable_handle<> rh) {
                    if (!s->Resume) {
                        ++subscription::pend();
                        print("%s - pending - %d\n", s->id.c_str(), (int64_t)subscription::pend());
                        s->Resume = rh;
                    }
                }

                void await_resume() {
                }
            };

            awaiter next() {
                return awaiter{this};
            }

            void resume(){
                if (Resume) {
                    --pend();
                    print("%s - pending - %d\n", id.c_str(), (int64_t)pend());
                    ex::resumable_handle<> resume{nullptr};
                    using std::swap;
                    swap(resume, Resume);
                    resume();
                }
            }

        };
    }

    template<class T>
    struct async_subject {
        struct SubjectState : public std::enable_shared_from_this<SubjectState>
        {
            bool Done = false;
            std::exception_ptr Error = nullptr;
            std::shared_ptr<T> CurrentValue;
            std::string id;

            std::set<std::shared_ptr<detail::subscription>> subscriptions;

            void pump() {
                auto keep = shared_from_this();
                auto subs = keep->subscriptions;
                for (auto& v : subs) {
                    v->resume();
                }
            }
        };
        std::shared_ptr<SubjectState> state = std::make_shared<SubjectState>();

        async_subject(std::string id = "") : state(std::make_shared<SubjectState>()) {state->id = id;}

        async::async_generator<T> subscribe() {
            auto local = this->state;
            auto s = std::make_shared<detail::subscription>(local->id);
            local->subscriptions.insert(s);
            bool cancel = false;
            __await add_oncancel([=, &cancel](){
                detail::print("%s - producer teardown\n", local->id.c_str());
                if (!local->Done){ cancel = true; s->resume(); }
            });
            while (!local->Done && !cancel) {
                detail::print("%s - producer await\n", local->id.c_str());
                __await s->next();
                detail::print("%s - producer resume next; done=%d, cancel=%d\n", local->id.c_str(), local->Done, cancel);
                if (local->Done || cancel) {
                    detail::print("%s - producer done\n", local->id.c_str());
                    break;
                }
                if (local->Error) {
                    std::rethrow_exception(local->Error);
                }
                detail::print("%s - producer yield; done=%d, cancel=%d\n", local->id.c_str(), local->Done, cancel);
                __yield_value *local->CurrentValue;
                detail::print("%s - producer resume yield\n", local->id.c_str());
            }
            local->subscriptions.erase(s);
            detail::print("%s - producer exit\n", local->id.c_str());
        }

        void next(T t) {
            if (state->Done || state->Error) {return;}
            if (!state->CurrentValue) {state->CurrentValue = std::make_shared<T>(std::move(t));} else {*state->CurrentValue = std::move(t);}
            state->pump();
        }
        void error(std::exception_ptr ep) {
            if (state->Done || state->Error) {return;}
            state->Error = ep;
            state->pump();
        }
        void complete() {
            if (state->Done || state->Error) {return;}
            state->Done = true;
            state->pump();
        }
    };

} }
