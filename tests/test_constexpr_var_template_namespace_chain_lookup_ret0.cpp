namespace outer {
template <class, class>
constexpr bool same_v = false;

template <class T>
constexpr bool same_v<T, T> = true;

namespace inner {
template <class T>
struct checker {
	static_assert(same_v<T, T>);
};
}
}

int main() {
	outer::inner::checker<int> c;
	(void)c;
	return 0;
}
