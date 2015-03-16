// yield.cpp : Defines the entry point for the console application.
//

#include <iostream>

#include <experimental/resumable>
#include <experimental/generator>
namespace ex = std::experimental;

ex::generator<int> fibonacci(int n) {
    int a = 0;
    int b = 1;

    while (n-- > 0) {
        __yield_value a;
        auto next = a + b; a = b;
        b = next;
    }
}

int wmain() {
    for (auto v : fibonacci(35)) {
        if (v > 10)
            break;
        std::cout << v << ' ';
    }
}
