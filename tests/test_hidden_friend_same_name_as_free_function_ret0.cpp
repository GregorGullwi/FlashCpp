// A hidden friend and a regular free function share the same name "get_value".
// The free function should be callable via ordinary unqualified lookup even
// though a hidden friend with the same name exists in a different class.
//
// This tests that the is_adl_only_function_name() check does not incorrectly
// reject calls to regular free functions that happen to share a name with a
// hidden friend defined in an unrelated class.
//
// Return value is 0 on success.
struct Widget {
	int value;
	friend int get_value(Widget& w) { return w.value; }
};

// Regular free function with the same name but different signature (no args)
int get_value() { return 42; }

int main() {
 // This should succeed: ordinary lookup finds the free function get_value()
 // even though a hidden friend named "get_value" exists in Widget.
	return get_value() - 42;	 // 42 - 42 == 0
}
