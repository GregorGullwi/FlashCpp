// Test that __is_nothrow_assignable selects the correct operator= overload
// when a struct has both copy and move assignment with mixed noexcept status.
struct S {
	int x;
	S& operator=(const S&) noexcept { return *this; }  // copy: noexcept
	S& operator=(S&&) { return *this; }                 // move: NOT noexcept
};

int main() {
	int result = 0;
	// copy assignment IS noexcept — should select operator=(const S&)
	if (__is_nothrow_assignable(S&, const S&))
		result += 21;
	// move assignment is NOT noexcept — should select operator=(S&&)
	if (!__is_nothrow_assignable(S&, S&&))
		result += 21;
	return result;  // expect 42
}
