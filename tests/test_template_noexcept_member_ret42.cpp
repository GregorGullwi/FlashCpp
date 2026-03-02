// Test that noexcept propagates correctly through template class assignment operators.
template<typename T>
struct Wrapper {
	T value;
	Wrapper& operator=(const Wrapper&) noexcept { return *this; }
};

int main() {
	if (__is_nothrow_assignable(Wrapper<int>&, const Wrapper<int>&))
		return 42;
	return 0;
}
