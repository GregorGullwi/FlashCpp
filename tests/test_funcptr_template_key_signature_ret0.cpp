// Test: function pointer template specialization matching preserves function_signature.
// Distinguishes R(*)(int) from R(*)(double).

template<typename F>
struct Id {
	int get() { return 0; }
};

template<typename R>
struct Id<R(*)(int)> {
	int get() { return 1; }
};

template<typename R>
struct Id<R(*)(double)> {
	int get() { return 2; }
};

int main() {
	Id<int(*)(int)> a;
	Id<int(*)(double)> b;
	return a.get() * 10 + b.get() - 12;
}
