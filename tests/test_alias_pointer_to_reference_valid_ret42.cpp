// A reference to a pointer is valid and must remain distinct from the
// ill-formed pointer-to-reference alias substitution.
template<class T>
using Ptr = T*;

using PointerReference = int*&;
using IntPointer = Ptr<int>;

int value = 7;

int main() {
	int* pointer = &value;
	PointerReference reference = pointer;
	IntPointer alias_pointer = &value;
	return *reference == 7 && *alias_pointer == 7 ? 42 : 1;
}
