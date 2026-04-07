struct BraceCallable {
	constexpr int operator()() const {
		return 21;
	}
} braceCallableEq = {}, braceCallableDirect{};

int main() {
	return (braceCallableEq() + braceCallableDirect()) == 42 ? 0 : 1;
}
