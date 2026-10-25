// Regression: ref-qualified members with identical signatures need distinct symbols.
struct RefQualified {
	int value;
	int call() & { return value + 10; }
	int call() && { return value + 20; }
	int operator-() & { return value + 30; }
	int operator-() && { return value + 40; }
};
struct RefBase {
	int inherited() & { return 51; }
	int inherited() && { return 61; }
};
template<class T> struct RefTemplate {
	T value;
	int read() const & { return static_cast<int>(value) + 70; }
	int read() const && { return static_cast<int>(value) + 80; }
};
struct RefDerived : RefBase {};
RefQualified* identity(RefQualified* pointer) { return pointer; }
int main() {
	RefQualified value{1};
	RefDerived derived;
	RefTemplate<short> templ{2};
	const RefTemplate<long> const_templ{3};
	if (identity(&value)->call() != 11) return 2;
	return value.call() == 11 && RefQualified{1}.call() == 21 && -value == 31 && -RefQualified{1} == 41 &&
		derived.inherited() == 51 && RefDerived{}.inherited() == 61 && templ.read() == 72 &&
		RefTemplate<short>{2}.read() == 82 && const_templ.read() == 73 && RefTemplate<long>{3}.read() == 83 ? 0 : 1;
}
