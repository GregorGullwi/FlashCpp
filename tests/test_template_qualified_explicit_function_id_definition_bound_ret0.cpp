// Regression: qualified explicit template-id calls inside template bodies must
// preserve their resolved member-template target through instantiation instead
// of falling back to same-name non-template overload recovery.

template <class T>
struct Box {
	template <class U>
	static int target(U) {
		return 1;
	}

	static int target(T) {
		return 100;
	}
};

template <class T>
int runQualifiedExplicitTarget() {
	return Box<T>::template target<T>(T{}) - 1;
}

int main() {
	return runQualifiedExplicitTarget<int>();
}
