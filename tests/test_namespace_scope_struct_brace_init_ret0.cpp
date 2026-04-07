struct BraceCallable {
	constexpr int operator()() const {
		return 21;
	}
} braceCallableEq = {}, braceCallableDirect{};

struct StatefulCallable {
	char offset;

	long operator()(long value) const {
		return value + offset;
	}
} statefulEq = {}, statefulDirect{};

int main() {
	long total = braceCallableEq() + braceCallableDirect();
	total += statefulEq(0L);
	total += statefulDirect(0L);
	return total == 42 ? 0 : 1;
}
