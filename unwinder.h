#pragma once
#define MAKE_IDENTIFIER_EXPLICIT_PASTER(Prefix, Suffix) Prefix ## Suffix

#define MAKE_IDENTIFIER_EXPLICIT_PASTER_1(Prefix, Suffix) MAKE_IDENTIFIER_EXPLICIT_PASTER(Prefix, Suffix)
#define MAKE_IDENTIFIER_EXPLICIT_PASTER_2(Prefix, Suffix) MAKE_IDENTIFIER_EXPLICIT_PASTER_1(Prefix, Suffix)
#define MAKE_IDENTIFIER_EXPLICIT_PASTER_3(Prefix, Suffix) MAKE_IDENTIFIER_EXPLICIT_PASTER_2(Prefix, Suffix)
#define MAKE_IDENTIFIER_EXPLICIT_PASTER_4(Prefix, Suffix) MAKE_IDENTIFIER_EXPLICIT_PASTER_3(Prefix, Suffix)
#define MAKE_IDENTIFIER_EXPLICIT_PASTER_5(Prefix, Suffix) MAKE_IDENTIFIER_EXPLICIT_PASTER_4(Prefix, Suffix)
#define MAKE_IDENTIFIER_EXPLICIT(Prefix, Suffix) MAKE_IDENTIFIER_EXPLICIT_PASTER_5(Prefix, Suffix)

#define MAKE_IDENTIFIER(Prefix) MAKE_IDENTIFIER_EXPLICIT(Prefix, __LINE__)

#define FAIL_FAST_FILTER() \
	__except(::unwinder::detail::FailFastFilter(GetExceptionInformation())) \
	{ \
	} do {} while(0,0)

#define FAIL_FAST_ON_THROW(Function) \
	::unwinder::detail::FailFastOnThrow((Function))

namespace unwinder { namespace detail {

        inline LONG WINAPI FailFastFilter(__in  struct _EXCEPTION_POINTERS *exceptionInfo)
        {
            RaiseFailFastException(exceptionInfo->ExceptionRecord, exceptionInfo->ContextRecord, 0);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        template<typename Function>
        auto FailFastOnThrow(Function&& function) -> decltype(std::forward<Function>(function)())
        {
            //
            // __ try must be isolated in its own function in order for the
            // compiler to reason about C++ unwind in the calling and called
            // functions.
            //
            __try
            {
                return std::forward<Function>(function)();
            }
            FAIL_FAST_FILTER();
        }

        template<typename Function>
        class unwinder
        {
        public:
            ~unwinder()
            {
                if (!!function)
                {
                    FAIL_FAST_ON_THROW([&]{(*function)();});
                }
            }

            explicit unwinder(Function* functionArg)
                : function(functionArg)
            {
            }

            void dismiss()
            {
                function = nullptr;
            }

        private:
            unwinder();
            unwinder(const unwinder&);
            unwinder& operator=(const unwinder&);

            Function* function;
        };
} }

#define ON_UNWIND(Name, Function) \
	ON_UNWIND_EXPLICIT(uwfunc_ ## Name, Name, Function)

#define ON_UNWIND_AUTO(Function) \
	ON_UNWIND_EXPLICIT(MAKE_IDENTIFIER(uwfunc_), MAKE_IDENTIFIER(unwind_), Function)

#define ON_UNWIND_EXPLICIT(FunctionName, UnwinderName, Function) \
	auto FunctionName = (Function); \
	::unwinder::detail::unwinder<decltype(FunctionName)> UnwinderName(std::addressof(FunctionName))
