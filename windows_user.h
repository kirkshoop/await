#pragma once

namespace windows_user {

    EXTERN_C IMAGE_DOS_HEADER __ImageBase;
    inline HINSTANCE GetCurrentInstance(){ return ((HINSTANCE)&__ImageBase); }

    template<typename Type>
    LRESULT CALLBACK WindowCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    template<typename Type>
    class window_class
    {
    public:
        static LPCWSTR Name() {
            return Type::class_name();
        }
        static ATOM Register()
        {
            WNDCLASSEX wcex = {};
            wcex.cbSize = sizeof(WNDCLASSEX);

            // defaults that can be overriden
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.hInstance = GetCurrentInstance();
            wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
            wcex.style         = 0;
            wcex.hIcon         = NULL;
            wcex.hCursor       = LoadCursor(NULL, IDC_ARROW);
            wcex.lpszMenuName  = NULL;

            Type::change_window_class(wcex);

            // not overridable
            wcex.lpszClassName = Name();
            wcex.lpfnWndProc = WindowCallback<Type>;

            return RegisterClassEx(&wcex);
        }

    private:
        ~window_class();
        window_class();
        window_class(window_class&);
        window_class& operator=(window_class&);
    };

    namespace detail {
        template<typename Type>
        std::unique_ptr<Type>
            find(HWND hwnd)
        {
            return std::unique_ptr<Type>(reinterpret_cast<Type*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
        }

        void erase(HWND hwnd)
        {
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        }

        template<typename Type>
        std::unique_ptr<Type> insert(
            HWND hwnd,
            std::unique_ptr<Type> type)
        {
            if (!type)
            {
                return nullptr;
            }

            SetLastError(0);

            auto result = SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(type.get()));
            LONG winerror = !result ? GetLastError() : ERROR_SUCCESS;

            if (!!winerror)
            {
                erase(hwnd);
                return nullptr;
            }

            if (!!result)
            {
                erase(hwnd);
                return nullptr;
            }

            return type;
        }

        template<typename Type>
        LRESULT CALLBACK WindowCallbackSafe(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto type = find<Type>(hWnd);

            if (message == WM_NCCREATE)
            {
                if  (type)
                {
                    // the slot where we would store our type instance is full. abort.
                    return FALSE;
                }
                type = insert(hWnd, std::unique_ptr<Type>(new (std::nothrow) Type(hWnd, reinterpret_cast<LPCREATESTRUCT>(lParam))));
                if (!type)
                {
                    return FALSE;
                }
            }

            LRESULT lResult = 0;
            bool handled = false;

            if (type)
            {
                std::tie(handled, lResult) = type->message(hWnd, message, wParam, lParam);
            }

            if (!handled)
            {
                lResult = DefWindowProc(hWnd, message, wParam, lParam);
            }

            if (message == WM_NCDESTROY)
            {
                erase(hWnd);
            }
            else
            {
                // not done yet.
                type.release();
            }

            return lResult;
        }
    }

    template<typename Type>
    LRESULT CALLBACK WindowCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        __try
        {
            return detail::WindowCallbackSafe<Type>(hWnd, message, wParam, lParam);
        }
        FAIL_FAST_FILTER();
        return 0;
    }
}
