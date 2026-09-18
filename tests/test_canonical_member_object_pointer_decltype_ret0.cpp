// A cast inside decltype flattens a member object pointer to the flat
// MemberObjectPointer category, which cannot carry the pointee. The parser
// keeps the pre-rewrite pointee specifier and the canonical adapter rebuilds a
// structural member object pointer instead of deferring the family. This
// reduced case keeps that declaration runnable and exercises a declarator-
// shaped member pointer alongside it.
struct MemberHost {
	int field;
	long other;
};

decltype(static_cast<int MemberHost::*>(nullptr)) selectMemberObject();

int main() {
	MemberHost host{42, 7};
	int MemberHost::* member = &MemberHost::field;
	if (sizeof(member) != sizeof(void*)) {
		return 1;
	}
	if (host.field != 42) {
		return 2;
	}
	return 0;
}
