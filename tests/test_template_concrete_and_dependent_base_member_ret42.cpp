// Tests that when a class template inherits from both a concrete base and a dependent
// base (has_deferred_base_classes = true), unqualified access to the concrete base's
// member still works correctly.  The has_deferred_base_classes guard prevents the
// lazy resolver from eagerly binding to the concrete base; the Unresolved codegen
// fallback resolves it correctly at instantiation time.
struct ConcreteBase {
	int x = 42;
};

template <typename DepBase>
struct D : ConcreteBase, DepBase {
	int get_x() { return x; }  // x lives in ConcreteBase, not in DepBase
};

struct Empty {};

int main() {
	D<Empty> d;
	return d.get_x();  // returns 42
}
