// A static member function has no implicit object, so an unqualified use of a
// non-static data member is ill-formed. The non-template form must be rejected
// during parsing rather than reaching codegen with a missing 'this'.

struct StaticUsesInstanceMember {
	int value;
	static int get() {
		return value;
	}
};

int main() {
	return StaticUsesInstanceMember::get();
}
