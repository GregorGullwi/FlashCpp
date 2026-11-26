// [dcl.init.ref]/5.4.3: the conversion result referent and the target referent
// may be related through a base class as long as the target adds qualification.
// operator const DerivedPart&() therefore binds a const volatile BasePart& and
// the volatile write is visible through the const volatile read.
struct BasePart {
	int value;
};

struct DerivedPart : BasePart { };

struct ConstDerivedSource {
	DerivedPart storage{11};

	operator const DerivedPart&() { return storage; }
};

int main() {
	ConstDerivedSource source;
	const volatile BasePart& alias = source;
	return alias.value == 11 ? 24 : 1;
}
