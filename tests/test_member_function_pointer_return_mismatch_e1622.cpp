// Binding the address of a member function whose return type differs from the
// pointer-to-member target must be rejected: [conv.mem] only permits an owner
// change, never a different callable signature.
struct C {
	const int& f() const;
};

int& (C::*p)() const = &C::f;

int main() {
	return 0;
}
