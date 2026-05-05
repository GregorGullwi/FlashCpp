int main() {
	static_assert(__is_complete_or_unbounded(0), "non-type argument must be rejected");
	return 0;
}
