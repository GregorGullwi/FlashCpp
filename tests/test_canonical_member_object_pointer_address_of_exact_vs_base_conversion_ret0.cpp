// Address-of a non-static data member must produce a member-object-pointer
// whose owner is the declaring class. Overload ranking then prefers Exact Match
// to that owner over [conv.mem] base-to-derived owner conversion.
struct BaseSelection {
	char marker[1];
};

struct DerivedSelection {
	char marker[2];
};

struct OwnSelection {
	char marker[3];
};

struct Base {
	int value;
};

struct Derived : Base {
	int own;
};

BaseSelection choose(int Base::*) {
	return {};
}

DerivedSelection choose(int Derived::*) {
	return {};
}

OwnSelection chooseOwn(int Derived::*) {
	return {};
}

static_assert(sizeof(decltype(choose(&Base::value))) == sizeof(BaseSelection));
static_assert(sizeof(decltype(choose(&Derived::value))) == sizeof(BaseSelection));
static_assert(sizeof(decltype(chooseOwn(&Derived::own))) == sizeof(OwnSelection));

int main() {
	return sizeof(decltype(choose(&Base::value))) == sizeof(BaseSelection) &&
			sizeof(decltype(choose(&Derived::value))) == sizeof(BaseSelection) &&
			sizeof(decltype(chooseOwn(&Derived::own))) == sizeof(OwnSelection)
		? 0
		: 1;
}
