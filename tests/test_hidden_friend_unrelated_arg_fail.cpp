// A hidden friend must not be found by ordinary unqualified lookup.
// Even though the call syntax is unqualified, ADL only considers associated
// namespaces/classes of the actual argument types. An `int` argument does not
// associate the enclosing class of the hidden friend, so this call is invalid.
struct Widget {
	friend int hidden(Widget) { return 1; }
};

int main() {
	return hidden(42);
}
