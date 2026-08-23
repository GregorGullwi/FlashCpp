int virtual_base_constructions = 0;

struct VirtualBase {
	int value;
	VirtualBase() : value(42) {
		++virtual_base_constructions;
	}
};

// B has an implicitly generated default constructor. Its virtual base must not
// be initialized by the base-object variant when B is used as a base of D.
template<typename T>
struct B : virtual VirtualBase {
	int b;
};

struct Prefix {
	int prefix;
	Prefix() : prefix(1) {}
};

// D also has an implicitly generated default constructor. The virtual base is
// shared by D and must be initialized in D's complete object.
template<typename T>
struct D : Prefix, virtual VirtualBase, B<T> {
	int d;
};

// The complete-object variant must also initialize virtual bases that are only
// reachable indirectly through a non-virtual base.
template<typename T>
struct Indirect : Prefix, B<T> {
	int d;
};

struct ExplicitWithVirtualBase : virtual VirtualBase {
	ExplicitWithVirtualBase() {}
};

// Synthetic lowering must also select the base-object variant when the direct
// base has an explicit constructor and therefore no implicit semantic plan.
template<typename T>
struct ThroughExplicit : ExplicitWithVirtualBase {
	T payload;
};

template<typename T>
using AliasBase = B<T>;

// A dependent alias must retain the canonical base identity through sema.
template<typename T>
struct ThroughAlias : AliasBase<T> {
	T payload;
};

// Repeated parameter spellings in separate templates still have distinct
// semantic identities; resolving this base must use this declaration's T.
template<typename T>
struct ThroughParameter : virtual T {
	int payload;
};

int main() {
	D<int> value;
	Indirect<long> indirect;
	ThroughExplicit<char> explicit_path;
	ThroughAlias<int> alias_path;
	ThroughParameter<VirtualBase> parameter_path;
	if (virtual_base_constructions != 5 ||
		value.value != 42 || indirect.value != 42 ||
		explicit_path.value != 42 || alias_path.value != 42 ||
		parameter_path.value != 42) {
		return 0;
	}
	return virtual_base_constructions * 8 + 2;
}
