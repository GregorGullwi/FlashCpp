template<bool B, typename T = void>
struct enable_if { using type = T; };

template<typename T>
struct enable_if<false, T> {};

template<bool B, typename T = void>
using enable_if_t = typename enable_if<B, T>::type;

template<typename T>
using global_cond_t = ::enable_if_t<(sizeof(T) >= 4), int>;

template<typename T>
global_cond_t<T> makeValue() {
	global_cond_t<T> x = 42;
	return x;
}

int main() {
	return makeValue<int>();
}
