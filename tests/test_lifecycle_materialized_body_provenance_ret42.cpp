// Phase 4 lifecycle regression: an out-of-line member body from a class
// template has deferred-template provenance, but once Materialized<T> is used
// it is a concrete body and must pass normalization and code generation.
//
// Keep the result dependent on both a narrow native type and a wide native
// type so the body cannot be accepted by accidentally skipping its casts or
// its aggregate temporary reconstruction.

struct MixedWidth {
	short narrow;
	long long wide;

	MixedWidth(short narrow_value, long long wide_value)
		: narrow(narrow_value), wide(wide_value) {}
};

template <typename T>
struct Materialized {
	T combine(T first, T second);
};

template <typename T>
T Materialized<T>::combine(T first, T second) {
	MixedWidth values(static_cast<short>(first), static_cast<long long>(second));
	return static_cast<T>(values.narrow + values.wide);
}

int main() {
	Materialized<int> ints;
	Materialized<long long> wide;
	return ints.combine(20, 22) == 42 && wide.combine(20, 22) == 42 ? 42 : 0;
}
