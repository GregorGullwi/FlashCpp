// Inline namespace names should be visible in the enclosing namespace

inline namespace global_v1 {
	int inline_value() { return 1; }
}

namespace outer {
	inline namespace v1 {
		constexpr int get() { return 41; }

		struct Foo {};
	}
}

int main() {
	// Lookup without qualifying the inline namespace name
	outer::Foo f;
	(void)f;
	return inline_value() + outer::get(); // 1 + 41 = 42
}
