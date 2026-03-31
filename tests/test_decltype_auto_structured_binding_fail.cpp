// decltype(auto) is not permitted in structured bindings per C++20 [dcl.struct.bind].
// The compiler should reject this with a compile error.

struct Pair {
	int first;
	int second;
};

int main() {
	Pair p{1, 2};
	decltype(auto)[a, b] = p;
	return a + b;
}
