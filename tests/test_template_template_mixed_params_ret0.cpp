// Regression test: template-template parameters must accept mixed parameter kinds
// (typename + non-type like int, bool) per C++20 [temp.param].
// Previously parsing failed with:
//   error: Expected 'typename' or 'class' in template template parameter form

template <typename T, int N>
struct wrap {
	using type = T;
	static constexpr int size = N;
};

template <typename T, bool B>
struct bwrap {
	using type = T;
	static constexpr bool flag = B;
};

template <typename T, int A, int B>
struct triple {
	static constexpr int sum = A + B;
};

// Template-template parameter with mixed typename + int
template <template <typename, int> class W>
struct probe_int {
	using result = W<int, 3>;
};

// Template-template parameter with mixed typename + bool
template <template <typename, bool> class W>
struct probe_bool {
	using result = W<int, true>;
};

// Template-template parameter with three params: typename, int, int
template <template <typename, int, int> class W>
struct probe_triple {
	using result = W<int, 10, 32>;
};

// Unnamed non-type parameters in template-template form (also valid C++20)
template <template <typename, int> class W>
struct probe_unnamed {
	using inner = W<int, 5>;
};

int main() {
	// Verify struct-level TTP with mixed params parses and instantiates correctly
	int a = probe_int<wrap>::result::size - 3;          // 0
	int b = probe_bool<bwrap>::result::flag ? 0 : 1;    // 0
	int c = probe_triple<triple>::result::sum - 42;     // 0
	int d = probe_unnamed<wrap>::inner::size - 5;       // 0

	return a + b + c + d;
}
