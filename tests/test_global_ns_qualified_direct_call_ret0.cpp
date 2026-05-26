// Regression: sema direct-call target recovery for global-qualified calls must
// preserve the leading :: and avoid broad same-name unqualified recovery.

namespace ns {
	struct Tag {};

	constexpr int pick(Tag) {
		return 0;
	}
}

constexpr int pick(ns::Tag) {
	return 42;
}

template <typename T>
int call() {
	return ::ns::pick(T{});
}

int main() {
	return call<ns::Tag>();
}
