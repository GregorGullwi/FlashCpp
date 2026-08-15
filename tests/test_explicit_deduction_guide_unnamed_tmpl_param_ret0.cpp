// C++17/C++20: an explicit deduction guide with an unnamed class-template
// parameter, matching MSVC STL `basic_string(basic_string_view<...>, ...)`.

template <class T>
struct View {
	T dummy{};
};

template <class T>
struct Box {
	T value;
	Box(T v) : value(v) {}
};

template <class T, class Alloc = int>
explicit Box(View<T>, const Alloc& = Alloc()) -> Box<T>;

int main() {
	View<short> vs{};
	View<long long> vl{};
	(void)vs;
	(void)vl;
	Box<int> named{7};
	return named.value - 7;
}
