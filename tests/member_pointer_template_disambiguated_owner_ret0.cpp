// [dcl.mptr] with [expr.prim.id.qual]: an explicit `::template` disambiguator
// may appear inside the nested-name-specifier of a member-pointer owner, so
// `int Outer<int>::template Inner<long>::*` names a member of that
// specialization. The owner must keep the specialization identity, not the
// primary member template, so the member pointer selects the right object.
template<class T>
struct Outer {
	template<class U>
	struct Inner {
		int value;
		T first;
	};
};

using ConcreteOwner = Outer<int>::Inner<long>;

int Outer<int>::template Inner<long>::*concrete_member = &ConcreteOwner::value;

int main() {
	ConcreteOwner object{17, 3};
	if (object.*concrete_member != 17) {
		return 1;
	}
	int replacement = 29;
	object.*concrete_member = replacement;
	if (object.value != 29 || object.first != 3) {
		return 2;
	}

	int (Outer<int>::template Inner<long>::*grouped) = &ConcreteOwner::value;
	object.*grouped = 7;
	if (object.value != 7) {
		return 3;
	}

	if (object.*concrete_member != 7) {
		return 4;
	}

	if (sizeof(int Outer<int>::template Inner<long>::*) != sizeof(int ConcreteOwner::*)) {
		return 5;
	}
	if (sizeof(int (Outer<int>::template Inner<long>::*)) != 8) {
		return 6;
	}
	return 0;
}
