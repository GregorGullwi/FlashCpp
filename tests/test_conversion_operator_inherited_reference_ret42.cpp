// An inherited conversion function is found on the base class and binds both a
// non-const and a const reference. The derived class mixes member sizes so the
// referenced member is not at the start of the object.
struct ReferenceBase {
	long long wide;

	operator long long&() { return wide; }
};

struct ReferenceDerived : ReferenceBase {
	char tag;
	int narrow;
};

int main() {
	ReferenceDerived holder{};
	holder.wide = 5000000000LL;
	holder.narrow = 7;
	long long& alias = holder;
	const long long& const_alias = holder;
	return alias == 5000000000LL && const_alias == 5000000000LL ? 42 : 1;
}
