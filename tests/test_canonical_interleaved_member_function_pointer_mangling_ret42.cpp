// The ordered member-function-pointer declarator for an interleaved return
// (`int* (Concrete::*)() const`) carries the return's Pointer component after
// the member-pointer component in its spine. MSVC and Itanium name mangling must
// fold that trailing component into the callable's return type, so a function
// whose parameter is this type mangles instead of aborting.
struct Concrete {};

using ConcretePointer = int* (Concrete::*)() const;

struct Selection {
	char marker[7];
};

struct Fallback {
	char marker[9];
};

Selection choose(ConcretePointer);
Fallback choose(...);

int main() {
	return sizeof(choose(static_cast<ConcretePointer>(nullptr))) == sizeof(Selection) ? 42 : 0;
}
