#pragma once

namespace async { namespace windows_user {

    struct async_messages
    {
        struct Result
        {
            LRESULT lres = 0;
            bool handled = false;
        };

        struct Message
        {
            template<UINT WM>
            static auto is() { return [](Message m){ return m.message == WM; }; }

            HWND hWnd;
            UINT message;
            WPARAM wParam;
            LPARAM lParam;
            Result* result;

            void handled() { result->handled = true; }
            void lresult(LRESULT lres) {result->lres = lres; }

            template<class T>
            T wparam_cast(){
                return *reinterpret_cast<T*>(std::addressof(wParam));
            }

            template<class T>
            T lparam_cast(){
                return *reinterpret_cast<T*>(std::addressof(lParam));
            }
        };

        template<class WPARAM_t = WPARAM, class LPARAM_t = LPARAM>
        struct TypedMessage
        {
            static auto as() { return [](Message m){return TypedMessage{m}; }; }

            TypedMessage(Message m)
                : hWnd(m.hWnd)
                , message(m.message)
                , wParam(m.wparam_cast<WPARAM_t>())
                , lParam(m.lparam_cast<LPARAM_t>())
                , result(m.result)
            {}

            HWND hWnd;
            UINT message;
            WPARAM_t wParam;
            LPARAM_t lParam;
            Result* result;

            void handled() { result->handled = true; }
            void lresult(LRESULT lres) {result->lres = lres; }
        };

        asub::async_subject<Message> sub;

        ~async_messages() {
            sub.complete();
        }

        std::tuple<bool, LRESULT> message(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
            Result result;
            auto m = Message{hWnd, message, wParam, lParam, &result};
            try {
                sub.next(m);
            } catch(...) {
                sub.error(std::current_exception());
            }
            return std::make_tuple(result.handled, result.lres);
        }

        async::async_generator<Message> messages() {
            return sub.subscribe();
        }

        template<UINT WM>
        async::async_generator<Message> messages() {
            return messages() | ao::filter(Message::is<WM>());
        }

        template<UINT WM, class WPARAM_t, class LPARAM_t = LPARAM>
        async::async_generator<TypedMessage<WPARAM_t, LPARAM_t>> messages() {
            return messages<WM>() | ao::map(TypedMessage<WPARAM_t, LPARAM_t>::as());
        }

    };

    template<class Derived, UINT WM>
    struct enable_send_call
    {
        static LRESULT send_call(HWND w, std::function<LRESULT(Derived&)> f) {
            auto fp = reinterpret_cast<LPARAM>(std::addressof(f));
            return SendMessage(w, WM_USER+1, 0, fp);
        }

        auto OnSendCall() -> std::future<void> {
            auto derived = static_cast<Derived*>(this);
            for __await (auto& m : derived->messages<WM, WPARAM, std::function<LRESULT(Derived&)>*>()) {
                m.handled(); // skip DefWindowProc
                m.lresult((*m.lParam)(*derived));
            }
        }

        enable_send_call() {
            OnSendCall();
        }
    };
} }
