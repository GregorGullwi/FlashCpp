// Regression test: nested virtual inheritance must be catchable through
// a deeply nested virtual base.
//
// Hierarchy:
//   DeepBase (virtual base of VBase)
//   VBase : virtual DeepBase
//   Left  : virtual VBase
//   Derived : Left
//
// collectReachableVBases only recurses into non-virtual bases, so when
// Left virtually inherits VBase, VBase is added to the vbase list but
// never explored.  DeepBase (a virtual base of VBase) is therefore
// missing from vbase_order, causing incorrect vtable prefix entries
// and typeinfo offset_flags.  catch(DeepBase&) will fail to match
// throw Derived{}.

struct DeepBase {
	virtual ~DeepBase() {}
	int deep = 42;
};

struct VBase : virtual DeepBase {
	int vb = 17;
};

struct Left : virtual VBase {
	int left = 1;
};

struct Derived : Left {
	int derived = 3;
};

int main() {
	// Test 1: catch through the immediate virtual base
	bool caught_vbase = false;
	try {
		throw Derived{};
	} catch (VBase& v) {
		if (v.vb != 17) return 1;
		caught_vbase = true;
	}
	if (!caught_vbase) return 2;

	// Test 2: catch through the deeply nested virtual base
	// This is the scenario that fails when collectReachableVBases
	// does not recurse into virtual bases.
	bool caught_deep = false;
	try {
		throw Derived{};
	} catch (DeepBase& d) {
		if (d.deep != 42) return 3;
		caught_deep = true;
	}
	if (!caught_deep) return 4;

	// Test 3: exact match still works
	bool caught_derived = false;
	try {
		throw Derived{};
	} catch (Derived& d) {
		if (d.derived != 3) return 5;
		caught_derived = true;
	}
	if (!caught_derived) return 6;

	return 0;
}
