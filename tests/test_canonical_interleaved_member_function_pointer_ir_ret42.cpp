// A value of an interleaved member-function-pointer type
// (`int* (Concrete::*)() const`) lowers as a fixed-size scalar: the ordered
// spine carries the return's Pointer component after the member-pointer
// component, but the object itself is a single member pointer. The declaration,
// parameter, return, and identifier IR paths must accept the ordered member
// pointer instead of failing closed on the non-projectable callable spine.
struct Concrete {
	int* run() const;
};

using ConcretePointer = int* (Concrete::*)() const;

int isNull(ConcretePointer pointer) {
	return pointer == nullptr ? 1 : 0;
}

ConcretePointer make() {
	return nullptr;
}

int main() {
	ConcretePointer pointer = make();
	ConcretePointer member = &Concrete::run;
	return isNull(pointer) && member != nullptr ? 42 : 0;
}
