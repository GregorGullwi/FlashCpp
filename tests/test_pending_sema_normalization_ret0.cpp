// Test: pending-sema normalization during late template instantiation
// This test verifies that templates instantiated during qualified lookup
// are properly normalized by semantic analysis before codegen uses them.
//
// Phase 3 regression test: ensures late-materialized templates participate
// in semantic analysis normalization before codegen.

template<typename T>
struct LazyHolder {
	using type = T;
	static constexpr int get_size() { return sizeof(T); }
};

template<typename T>
struct Wrapper {
	using type = typename LazyHolder<T>::type;

	// This member function's return type depends on a late-materialized alias
	// When Wrapper<int> is instantiated, LazyHolder<int>::type must be
	// normalized before get_value() can be correctly compiled
	static constexpr type get_value() {
		return T{};
	}

	static constexpr int get_inner_size() {
		return LazyHolder<T>::get_size();
	}
};

template<typename T>
struct ChainedLookup {
	// Tests that nested template instantiations triggered by qualified
	// lookup are all normalized before use
	using inner_type = typename Wrapper<T>::type;  // Should trigger Wrapper<T> instantiation

	static constexpr int compute() {
		return Wrapper<T>::get_inner_size();
	}
};

int main() {
	// Trigger lazy instantiation chain: ChainedLookup<int> -> Wrapper<int> -> LazyHolder<int>
	constexpr int size = ChainedLookup<int>::compute();

	// Verify through Wrapper directly
	constexpr int direct_val = Wrapper<int>::get_value();

	// Size of int should be 4 bytes
	// Return 0 if normalization worked correctly (size == 4 and direct_val == 0)
	// Return 1 if normalization failed (incorrect values indicate sema did not run)
	return (size == 4 && direct_val == 0) ? 0 : 1;
}
