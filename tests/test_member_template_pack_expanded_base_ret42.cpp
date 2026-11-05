// A member class-template may use a variadic base pack such as `List : U...`.
struct A {
	int a = 1;
};

struct B {
	int b = 2;
};

struct Owner {
	template<class... U>
	struct List : U... {};
};

int main() {
	Owner::List<A, B> value{};
	if (value.a != 1) return 1;
	if (value.b != 2) return 2;
	return 42;
}
