// Regression: a member call on a non-template class receiver inside a
// function template must delay overload resolution when an argument is a
// function-parameter pack ([temp.over], [temp.dep.expr]). The unexpanded
// pack is one syntactic argument and must not be arity-checked against the
// concrete overload set during template-body materialization.
// The class-template path with a 1-argument overload is covered by
// test_member_call_pack_expansion_ret0.cpp; this test uses overloads whose
// arities do not match the unexpanded pack.

struct Pair {
	short a;
	int b;
};

struct Combiner {
	int combine(char a, short b) { return a + b; }
	int combine(int a, long long b, Pair p) { return a + static_cast<int>(b) + p.a + p.b; }
};

struct Adder {
	int add(int a, int b) { return a + b; }
};

template <typename... Args>
int invoke(Args... args) {
	Combiner combiner;
	return combiner.combine(args...);
}

template <typename... Args>
int add_pair(Args... args) {
	Adder adder;
	return adder.add(args...);
}

template <typename... Args>
struct Invoker {
	Combiner combiner;
	int invoke(Args... args) {
		return combiner.combine(args...);
	}
};

int main() {
	if (invoke(static_cast<char>(2), static_cast<short>(5)) != 7)
		return 1;
	Pair p{3, 4};
	if (invoke(1, 2LL, p) != 10)
		return 2;
	if (add_pair(3, 4) != 7)
		return 3;

	Invoker<char, short> i1;
	if (i1.invoke(static_cast<char>(2), static_cast<short>(5)) != 7)
		return 4;
	Invoker<int, long long, Pair> i2;
	if (i2.invoke(1, 2LL, p) != 10)
		return 5;
	return 0;
}
