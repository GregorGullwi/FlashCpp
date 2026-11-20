// [temp.inst]/1 exposes a static data member's type at class-template
// instantiation. A member pointer whose pointee is an array has no canonical
// representation in the declarator importer yet, so the substituted type must
// keep fail-closed instead of publishing a flattened pointer identity.
struct Target {
	int cells[3];
};

template<class T>
struct Holder {
	static int (T::*pointer)[3];
};

Holder<Target> object;
