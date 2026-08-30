// Regression: an if-init declaration is in scope for the condition and both
// branches, but must not leak into the statement following the if.
template <typename T>
struct ScopeBox {
	T value;

	int test() {
		if (auto& scoped = value; true) {
			(void)scoped;
		}
		auto& leaked = scoped;
		return leaked;
	}
};

int main() {
	ScopeBox<int> box;
	return box.test();
}
