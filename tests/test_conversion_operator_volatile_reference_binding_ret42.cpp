// [dcl.init.ref]/5.1.2 (CWG 1571): a conversion function result adding volatile
// qualification binds a volatile int& and a const volatile int& directly. The
// referenced member is mutated through the volatile alias and read back through
// every reference, so a temporary copy or a dropped qualification changes the
// result.
struct MixedVolatileHolder {
	char tag;
	int narrow;
	long long wide;

	operator volatile int&() { return narrow; }
};

int read_const_volatile(const volatile int& value) {
	return value;
}

int main() {
	MixedVolatileHolder holder{1, 20, 4000000000LL};
	volatile int& mutable_alias = holder;
	mutable_alias += 7;
	const volatile int& const_alias = holder;
	return read_const_volatile(holder) + const_alias + mutable_alias - 39;
}
