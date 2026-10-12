// Two base classes each declare a nested enum with the same name, so an
// unqualified reference to it inside a member function is ambiguous and must
// report AmbiguousQualifiedLookup.

struct Left {
	enum Kind { First };
};

struct Right {
	enum Kind { Second };
};

struct Both : Left, Right {
	int read() {
		return Kind::First;
	}
};

int main() {
	Both value;
	return value.read();
}
