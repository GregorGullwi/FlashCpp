// An unreferenced derived class must not define its implicit copy/move
// constructors, and therefore must not synthesize or queue definitions for a
// class-template base that has no constructor AST.
template <class T>
struct Base {
	T value;
};

struct Derived : Base<short> {
	int tag;
};

struct VirtualDerived : virtual Base<short> {
	int tag;
};

int main() {
	return 0;
}
