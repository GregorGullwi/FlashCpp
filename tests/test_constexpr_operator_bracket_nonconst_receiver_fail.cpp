// Expected failure: non-const receiver prefers non-const operator[] overload.
// The selected overload is not constexpr, so this cannot be a core constant expression.
struct Wrap {
	int val;
	constexpr int operator[](int) const { return val; }
	int& operator[](int) { return val; }
};

constexpr int nonConstReceiverTest() {
	Wrap w{99};
	return w[0];
}

static_assert(nonConstReceiverTest() == 99);

int main() { return 0; }
