// Hardening test: nested struct with scalar (non-struct) static members
// used in default member initializers.
template <int A, int B>
struct Math {
	struct Result {
		static constexpr int sum = A + B;
		static constexpr int product = A * B;
		int total = sum + product;
	};
};

int main() {
	Math<3, 5>::Result r;
	if (r.total != 23) return r.total;  // 3+5 + 3*5 = 8+15 = 23
	return 42;
}
