// Regression: namespace-qualified explicit template-id calls inside template
// bodies must preserve their qualified/direct-call binding instead of later
// rebinding to an unqualified same-name overload.

namespace library {
	template <class U>
	int target(U) {
		return 1;
	}
}

int target(int) {
	return 100;
}

template <class T>
int runQualifiedExplicitTarget() {
	return library::target<T>(T{}) - 1;
}

int main() {
	return runQualifiedExplicitTarget<int>();
}
