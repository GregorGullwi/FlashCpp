// Regression: sema direct-call target recovery for relative namespace-qualified
// calls must stay bound to the qualified owner instead of falling back to a
// same-name outer overload.

namespace outer {
	namespace inner {
		struct Tag {};

		constexpr int pick(Tag) {
			return 0;
		}
	}

	constexpr int pick(inner::Tag) {
		return 42;
	}

	template <typename T>
	int call() {
		return inner::pick(T{});
	}
}

int main() {
	return outer::call<outer::inner::Tag>();
}
