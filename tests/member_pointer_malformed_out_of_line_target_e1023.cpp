// A member-pointer declarator must not swallow a malformed out-of-line target:
// the `int* C::*` head is consumed as a declarator, so the trailing `C::` still
// needs a member name and reports OutOfLineDefinitionMalformedTarget (1023).
struct C {
	int* pointer;
};

int* C::*C::;

int main() {
	return 0;
}
