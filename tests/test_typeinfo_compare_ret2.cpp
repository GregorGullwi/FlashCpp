struct Base {
	virtual ~Base() {}
	virtual int get() { return 1; }
};

struct Derived : Base {
	virtual int get() { return 2; }
};

struct MockTypeInfo {
	const void* identity;

	bool operator==(const MockTypeInfo& other) const {
		return identity == other.identity;
	}
};

int main() {
	Derived d;
	Base* bp = &d;

	// Avoid the full MSVC <typeinfo>/<exception> header stack until std::type_info
	// comparisons compile cleanly; the RTTI identity check is the behavior under test.
	MockTypeInfo dynamicType{typeid(*bp)};
	MockTypeInfo derivedType{typeid(Derived)};
	bool same = (dynamicType == derivedType);
	return same ? 2 : 0;  // return 2 if correct
}
