int conversions = 0;

struct B { int x; };
struct Source {
	operator B() const {
		++conversions;
		return B{7};
	}
};
struct Derived : B { int tail; };
struct MemberHolder { B member; int tail; };

struct S { int x, y; };
struct PointerBase { S* values[1]; };
struct PointerDerived : PointerBase { int tail; };

struct NestedBase { int values[2][2]; };
struct NestedDerived : NestedBase { int tail; };

int main() {
	Source source;
	Derived converted{source, 3};
	if (converted.x != 7 || converted.tail != 3 || conversions != 1) return 1;

	Derived temporary{Source{}, 4};
	if (temporary.x != 7 || temporary.tail != 4 || conversions != 2) return 2;

	MemberHolder member{Source{}, 10};
	if (member.member.x != 7 || member.tail != 10 || conversions != 3) return 3;

	PointerDerived null_pointer{nullptr, 5};
	if (null_pointer.values[0] != nullptr || null_pointer.tail != 5) return 4;

	S object{6, 7};
	PointerDerived pointer{&object, 8};
	if (pointer.values[0] != &object || pointer.tail != 8) return 5;

	NestedDerived nested{{1, 2, 3, 4}, 9};
	if (nested.values[0][0] != 1 || nested.values[0][1] != 2 || nested.values[1][0] != 3 ||
		nested.values[1][1] != 4 || nested.tail != 9) return 6;

	return 0;
}
