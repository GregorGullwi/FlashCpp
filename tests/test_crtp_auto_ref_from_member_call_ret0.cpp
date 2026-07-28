// Regression: CRTP base members that deduce auto& from an unqualified
// same-class call (view_interface::_Cast shape) must rebind the pattern
// return type (Derived&) to the concrete argument before local auto
// deduction, including when the callee is still only lazily materialized.
template <typename Derived>
struct ViewInterface {
	Derived& cast() {
		return static_cast<Derived&>(*this);
	}

	const Derived& cast() const {
		return static_cast<const Derived&>(*this);
	}

	bool empty() {
		auto& self = cast();
		return self.value == 0;
	}

	bool empty() const {
		auto& self = cast();
		return self.value == 0;
	}

	int read() {
		auto& self = cast();
		return self.value;
	}
};

template <typename T>
struct Subrange : ViewInterface<Subrange<T>> {
	T value{};
};

int main() {
	Subrange<int> mutable_range;
	mutable_range.value = 0;
	if (!mutable_range.empty()) {
		return 1;
	}

	const Subrange<int> const_range = mutable_range;
	if (!const_range.empty()) {
		return 2;
	}

	Subrange<long long> wide;
	wide.value = 21;
	auto& wide_self = wide.cast();
	if (wide_self.value != 21) {
		return 3;
	}

	return wide.read() == 21 ? 0 : 4;
}
