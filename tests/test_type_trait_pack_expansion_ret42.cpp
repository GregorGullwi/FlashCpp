// Test pack expansion in variadic type traits like __is_constructible

template<bool V>
struct bool_constant {
	static constexpr bool value = V;
};

// Variadic type trait pattern with pack expansion - similar to __is_constructible_impl
template<typename T, typename... Args>
struct is_constructible_wrapper {
	// Use __is_constructible with pack expansion
	static constexpr bool value = __is_constructible(T, Args...);
};

// Simple struct with constructors
struct Point {
	int x, y;
	Point() : x(0), y(0) {}
	Point(int a) : x(a), y(0) {}
	Point(int a, int b) : x(a), y(b) {}
};

int main() {
	// These should compile - testing that pack expansion syntax parses correctly
	is_constructible_wrapper<int> t1;
	is_constructible_wrapper<int, int> t2;
	is_constructible_wrapper<Point> t3;
	is_constructible_wrapper<Point, int> t4;
	is_constructible_wrapper<Point, int, int> t5;
	
	return 42;
}
