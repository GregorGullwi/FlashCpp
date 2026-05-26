// Regression: sema direct-call recovery for root-qualified names like ::pick()
// must treat the empty qualifier prefix as the global namespace instead of
// falling back to same-name local namespace lookup.

int pick() {
	return 42;
}

namespace ns {
	int pick() {
		return 0;
	}

	template <typename T>
	int call() {
		return ::pick();
	}
}

int main() {
	return ns::call<int>();
}
