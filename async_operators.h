#pragma once

template<typename T, typename P>
async_generator<T> filter(async_generator<T> s, P p) {
	for __await(auto& v : s) {
		if (p(v)) {
			__yield_value v;
		}
	}
}

template<typename T, typename M>
async_generator<decltype(M(declval(T)))> map(async_generator<T> s, M m) {
	for __await(auto& v : s) {
		__yield_value m(v);
	}
}
