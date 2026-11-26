// [dcl.init.ref]/5.1.2: operator int&() binds a const int& directly instead of
// materializing a temporary. Writing through the mutable alias must be visible
// through the const reference.
struct MixedConstAliasHolder {
	long long wide;
	int narrow;
	char tag;

	operator int&() { return narrow; }
};

int main() {
	MixedConstAliasHolder holder{4000000000LL, 17, 3};
	int& mutable_alias = holder;
	const int& const_alias = holder;
	mutable_alias += 16;
	return const_alias == 33 ? 33 : 1;
}
