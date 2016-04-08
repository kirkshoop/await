These projects explore the [coroutine proposal](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4134.pdf) as implemented in [Visual Studio 2015 Update 2](https://www.visualstudio.com/news/vs2015-update2-vs). It includes an `async_generator<T>` and some algorithms and adaptors to use it.

### Async Operators Example
```cpp
future<void> waitfor() {
    for co_await(auto v : fibonacci(10) | 
        copy_if([](int v) {return v % 2 != 0; }) |
        transform([](int i) {return to_string(i) + ","; }) | 
        delay(1s)
    ) {
        std::cout << v << ' ';
    }
}

int wmain() {
    waitfor().get();
}
```

This code is in the **async** project. This code looks similar to [Eric Niebler's](https://twitter.com/ericniebler) Range proposal ([GitHub](https://github.com/ericniebler/range-v3), [Blog](http://ericniebler.com/)), but these are async ranges. Not only are the types involved different, but also the for loop and the algorithms. Coordinating many Ranges from many threads over time has additional complexity and different algorithms. The [ReactiveExtensions](http://reactivex.io/languages.html) family of libraries provide a lot of algorithms useful for async Ranges. The [RxMarbles](http://rxmarbles.com/) site has live diagrams for many of the algorithms. [rxcpp](https://github.com/Reactive-Extensions/RxCpp) implements some of these algorithms in C++ without await.

### Values distributed in Time
This is a table of types that represent each combination of values and time.

 | Value | Sequence
--------|----------|-------------
past | `T` | `vector<T>`
lazy | `[]() -> T { . . . }` | `generator<T>`
later | `future<T>` | `async_generator<T>`

* past - A value has already been produced before the caller asks for it.
* lazy -  A value is produced when asked for by a caller.
* later - When a value is produced the caller is resumed.

The three that I explore in the context of await are `future<T>`, `generator<T>` and `async_generator<T>`.

### `future<T>` - the value arrives Later
This is covered first because yield_value is composed on top of await. `future<T>` represents a value of T that may become available later. It may hold an exception instead. A function that returns `future<T>` is allowed to be called using `co_await`.

### `generator<T>` - each value is Lazy
`generator<T>` implements the Range Concept so it works with existing algorithms from STL and Rangev3 and the `range-for` feature. It can be found in the header `experimental/generator`. A function that returns `generator<T>` is allowed to use the `co_yield` keyword (which evaluates to `co_await generator<T>::promise_type::yield_value(T)`).

### `async_generator<T>` - each value arrives Later
`async_generator<T>` implements a new AsyncRange Concept. `begin()` and `++iterator` return an awaitable type that produces an iterator later while `end()` returns an `iterator` immediately. A new set of algorithms is needed and a new `async-range-for` has been added that inserts `co_await` into `i = co_await begin()` and `co_await ++i`. A function that returns  `async_generator<T>` is allowed to use `co_await` and `co_yield`.

### Resources
* [Gor Nishanov ](https://twitter.com/gornishanov) kindly answered many emails as I worked on the code. His epic presentation ([PDF](https://github.com/CppCon/CppCon2014/blob/master/Presentations/await%202.0%20-%20Stackless%20Resumable%20Functions/await%202.0%20-%20Stackless%20Resumable%20Functions%20-%20Gor%20Nishanov%20-%20CppCon%202014.pdf), [YouTube](https://www.youtube.com/watch?v=KUhSjfSbINE)) of the design, implementation and sample usage at [CPPCON 2014](http://cppcon.org/) is required watching.
* James McNellis made a great presentation for MeetingC++ [PDF](https://meetingcpp.com/tl_files/mcpp/2015/talks/James%20McNellis%20-%20Coroutines%20-%20%20Meeting%20C++%202015.pdf), [YouTube](https://www.youtube.com/watch?v=YYtzQ355_Co)
