// Expected failure: only a non-constexpr operator[] exists.
// Calling it in constant evaluation is ill-formed.
struct Wrap {
	int val;
	int& operator[](int) { return val; }
};

constexpr int nonConstReceiverTest() {
	Wrap w{99};
	return w[0];
}

static_assert(nonConstReceiverTest() == 99);

int main() { return 0; }
