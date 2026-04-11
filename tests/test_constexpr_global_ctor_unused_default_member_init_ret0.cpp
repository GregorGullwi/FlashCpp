// Regression test for Bug 1 (ignore_default_initializer_errors).
//
// The struct has two members: 'value' (set by the constructor) and 'tag'
// (with a default member initializer that uses a constexpr helper function).
// The member function get_value() only uses 'value'.
//
// Before this PR, extract_object_members called materialize_members_from_constructor
// with ignore_default_initializer_errors=true, so if the evaluator failed to
// evaluate the default initializer for 'tag', it would still succeed and
// populate 'value'.  After the PR, the helper hardcodes false, which would
// cause the entire materialization to fail if 'tag's default initializer
// cannot be evaluated.
//
// This test should return 0 on success.

constexpr int make_tag() { return 42; }

struct Widget {
	int value;
	int tag = make_tag();

	constexpr Widget(int v) : value(v) {}

	constexpr int get_value() const { return value; }
};

constexpr Widget g{7};

static_assert(g.get_value() == 7);

int main() {
	return g.get_value() == 7 ? 0 : 1;
}
