// Test: pack expansion into template specializations, e.g. Box<Ts>... args
// Exercises the fix that extracts the pack name from dependent_name in TypeInfo
// rather than naively taking the token (which would give "Box", not "Ts").

template<typename T>
struct Box {
	T value;
	Box(T v) : value(v) {}
};

template<int N, typename... Ts>
int f(Box<Ts>... /*boxes*/) {
	return N;
}

int main() {
	Box<int> a(1);
	Box<double> b(2.0);
	// Explicit N=0; Ts deduced as <int, double> from the two Box arguments.
	return f<0>(a, b);
}
