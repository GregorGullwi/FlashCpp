// Regression: inferExpressionType for unary +/-/~ on a struct previously
// returned a generic nativeTypeIndex(Struct) placeholder rather than {} or
// the actual user-defined operator's return type. That caused a same-type
// copy-init like `Big b = -a;` to miscompare canonical type ids and select
// an explicit converting constructor (when one was declared via a
// forward-declared type) — emitting a spurious "Cannot use copy
// initialization with explicit constructor" diagnostic. This reproduces the
// libstdc++ <ranges>::__detail::__max_size_type `operator-` shape.

struct Other;

struct Big {
	long _M_val;

	Big() = default;
	Big(long v) : _M_val(v) {}

	// explicit converting ctor that takes a forward-declared type — declared
	// only, never defined or called. Its mere presence used to fool the
	// converting-ctor scan when the source's canonical id did not exactly
	// equal the target's.
	explicit Big(const Other& d);

	Big operator-() const { return Big{-_M_val}; }
};

int main() {
	Big a(5);
	Big b = -a;	   // same-type copy-init from unary operator-
	return static_cast<int>(b._M_val) == -5 ? 0 : 1;
}

