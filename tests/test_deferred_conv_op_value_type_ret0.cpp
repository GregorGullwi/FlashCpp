// Verify: deferred body replay finds the stub via identity map (not name scan).
// The static const int tag member forces deferred body replay for all inline
// member bodies (because the struct type must be fully registered before any
// member body that references a dependent name can be parsed).
// Expected return: 0

template<typename T>
struct Wrapper {
	static const int tag = 1;  // forces deferred body replay
	using value_type = T;
	T value_;
	Wrapper(T v) : value_(v) {}
	operator value_type() const { return value_; }
};

int main() {
	Wrapper<int> w(42);
	int x = w;
	return x - 42;  // 0 if correct
}
