// An indirectly recursive alias template must report the implementation-limit
// diagnostic instead of overflowing the native stack during materialization.
template <class T>
struct Wrapper {
	using type = T;
};

template <class T>
using A = typename Wrapper<A<T>>::type;

int main() {
	A<int> value = 0;
	(void)value;
	return 0;
}
