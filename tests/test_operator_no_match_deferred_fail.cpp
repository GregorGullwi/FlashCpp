// Deferred operator no-match should still be rejected after template substitution.

struct NoPlus {
	int value;
};

template<typename T>
int addValues(T lhs, T rhs) {
	auto sum = lhs + rhs;
	return sum.value;
}

int main() {
	NoPlus value{7};
	return addValues(value, value);
}