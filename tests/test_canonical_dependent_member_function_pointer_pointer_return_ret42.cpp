// A dependent member-function-pointer whose callable return carries its own
// declarator (here `T*`) is an ordered declarator whose member-pointer component
// is not innermost: `T* (Concrete::*)() const`. The canonical importer must fold
// the spine's inner return components into the callable's return type and keep
// the owner TypeId, so the type promotes structurally instead of failing
// semantic canonicalization.
struct Concrete {};

template <class T>
int measure() {
	using MemberPointer = T* (Concrete::*)() const;
	static_assert(__is_same(MemberPointer, int* (Concrete::*)() const));
	return noexcept(static_cast<MemberPointer>(nullptr) == nullptr) ? 42 : 0;
}

int main() {
	return measure<int>();
}
