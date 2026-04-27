// Test ADL with inline namespaces per C++20 [basic.lookup.argdep]/2.
//
// Parent transparency: when the argument type lives in an inline namespace,
// the enclosing (parent) namespace is also searched.
//
// Child transparency: when the argument type lives in a parent namespace that
// has inline children, those inline namespaces are also searched.

namespace outer {
	// process() lives in the parent namespace
	template <typename T>
	int process(T) { return 42; }

	inline namespace inner {
		struct ChildType {};
	}

	struct ParentType {};
}

namespace outer {
	inline namespace inner {
		// from_inner() lives in the inline child namespace
		int from_inner(outer::ParentType) { return 20; }
	}
}

int main() {
	outer::inner::ChildType c;
	outer::ParentType p;

	// Parent transparency: ChildType is in outer::inner (inline), so outer is
	// also searched and process<ChildType> is found.
	int r1 = process(c);

	// Child transparency: ParentType is in outer, so outer::inner is also
	// searched and from_inner(ParentType) is found.
	int r2 = from_inner(p);

	return r1 - r2 - 22; // 42 - 20 - 22 = 0
}
