// A concept requirement is an atomic constraint whose operand is often a
// substituted template parameter. The lazy constraint evaluator used to answer
// an unclassified trait by silently returning "satisfied", so `!Trait<T>`
// rejected every candidate and `Trait<T>` accepted every one. The canonical
// structural classification now owns the traits it can classify, and an
// unclassified trait is an explicit unknown outcome that neither proves
// satisfaction nor is inverted into a failure.
//
// Each two-way probe pairs one constrained overload with an unconstrained
// fallback, so both directions are observable at run time: the argument whose
// trait holds selects the constrained candidate and the argument whose trait
// does not hold falls through. A regression that made the classification
// permissive would select the fallback for a matching argument, and one that
// made it over-strict would fail to compile here.
struct Widget {
	int value;
	long tag;
	void bump();
};

enum class Scoped : unsigned short { A = 1 };
enum Plain { B = 2 };

template <typename T>
struct Box {
	T value;
	T* pointer;
};

void Widget::bump() {}

template <typename T>
concept Pointerish = __is_pointer(T);
template <typename T>
concept Integralish = __is_integral(T);
template <typename T>
concept Floatingish = __is_floating_point(T);
template <typename T>
concept Enumish = __is_enum(T);
template <typename T>
concept Scalarish = __is_scalar(T);
template <typename T>
concept Objectish = __is_object(T);
template <typename T>
concept Compoundish = __is_compound(T);
template <typename T>
concept SameAsInt = __is_same(T, int);
template <typename T>
concept Arrayish = __is_array(T);
template <typename T>
concept MemberObjectPointerish = __is_member_object_pointer(T);
// A variadic constructibility requirement stays unclassified, so `!` still
// propagates an Unknown outcome. The zero-argument form is now classified and
// is covered by test_canonical_lazy_default_construction_concept_ret0.cpp.
template <typename T>
concept NotConstructibleFromInt = !__is_constructible(T, int);
template <typename T>
concept PointerOrUnclassified = Pointerish<T> || __is_constructible(T);
template <typename T>
concept PointerAndUnclassified = Pointerish<T> && __is_constructible(T);

int probePointer(Pointerish auto) { return 1; }
int probePointer(...) { return 2; }

int probeIntegral(Integralish auto) { return 3; }
int probeIntegral(...) { return 4; }

int probeFloating(Floatingish auto) { return 5; }
int probeFloating(...) { return 6; }

int probeEnum(Enumish auto) { return 7; }
int probeEnum(...) { return 8; }

int probeScalar(Scalarish auto) { return 9; }
int probeScalar(...) { return 10; }

int probeCompound(Compoundish auto) { return 13; }
int probeCompound(...) { return 14; }

int probeSameAsInt(SameAsInt auto) { return 15; }
int probeSameAsInt(...) { return 16; }

int probeMemberPointer(MemberObjectPointerish auto) { return 30; }
int probeMemberPointer(...) { return 31; }

int probeObject(Objectish auto) { return 11; }

int probeArray(Arrayish auto&) { return 28; }

int probeNegatedUnknown(NotConstructibleFromInt auto) { return 17; }

int probeDisjunctionUnknown(PointerOrUnclassified auto) { return 19; }

int probeConjunctionUnknown(PointerAndUnclassified auto) { return 21; }

template <typename T>
struct Holder {
	template <Pointerish U>
	static int pointerMember(U) {
		return 40;
	}
	template <Objectish U>
	static int objectMember(U) {
		return 41;
	}
};

int main() {
	int scalar = 0;
	int* pointer = &scalar;
	int array[3] = {};
	Widget widget{};
	Scoped scoped = Scoped::A;
	Plain plain = B;
	double dbl = 0;
	int Widget::* memberPointer = &Widget::value;
	Box<int> box{};

	int mismatches = 0;

	if (probePointer(pointer) != 1) {
		mismatches |= 1;
	}
	if (probePointer(widget) != 2) {
		mismatches |= 2;
	}
	if (probeIntegral(scalar) != 3) {
		mismatches |= 3;
	}
	if (probeIntegral(widget) != 4) {
		mismatches |= 4;
	}
	if (probeFloating(dbl) != 5) {
		mismatches |= 5;
	}
	if (probeFloating(scalar) != 6) {
		mismatches |= 6;
	}
	if (probeEnum(scoped) != 7) {
		mismatches |= 7;
	}
	if (probeEnum(scalar) != 8) {
		mismatches |= 8;
	}
	if (probeScalar(pointer) != 9) {
		mismatches |= 9;
	}
	if (probeScalar(widget) != 10) {
		mismatches |= 10;
	}
	if (probeCompound(pointer) != 13) {
		mismatches |= 11;
	}
	if (probeCompound(scalar) != 14) {
		mismatches |= 12;
	}
	if (probeSameAsInt(scalar) != 15) {
		mismatches |= 13;
	}
	if (probeSameAsInt(scoped) != 16) {
		mismatches |= 14;
	}
	if (probeMemberPointer(memberPointer) != 30) {
		mismatches |= 15;
	}
	if (probeMemberPointer(scalar) != 31) {
		mismatches |= 16;
	}
	// `int` and a record are both object types, so this probe only asserts
	// acceptance; no reference or function type can reach a by-value parameter.
	if (probeObject(widget) != 11) {
		mismatches |= 17;
	}
	if (probeObject(box) != 11) {
		mismatches |= 18;
	}
	if (probeObject(scalar) != 11) {
		mismatches |= 19;
	}
	// An array keeps its bounds through a reference parameter, so the array
	// probe is an acceptance check too.
	if (probeArray(array) != 28) {
		mismatches |= 20;
	}
	// Constructibility has a variadic argument list and remains outside the
	// unary canonical record-property schema, so its requirement is unknown in
	// lazy constraints rather than being treated as proof or inverted by `!`.
	if (probeNegatedUnknown(widget) != 17) {
		mismatches |= 21;
	}
	if (probeNegatedUnknown(pointer) != 17) {
		mismatches |= 22;
	}
	// A disjunction with an unknown side, and a conjunction whose proven-true
	// side is paired with an unknown side, both stay unknown rather than
	// becoming a hard failure.
	if (probeDisjunctionUnknown(widget) != 19) {
		mismatches |= 23;
	}
	if (probeDisjunctionUnknown(pointer) != 19) {
		mismatches |= 24;
	}
	if (probeConjunctionUnknown(pointer) != 21) {
		mismatches |= 25;
	}
	// A class-template member reaches the same evaluator through the
	// instantiation's own outer template arguments.
	if (Holder<int>::pointerMember(pointer) != 40) {
		mismatches |= 26;
	}
	if (Holder<int>::objectMember(widget) != 41) {
		mismatches |= 27;
	}
	{
		const int expected = 28;
		(void)expected;
		if (probeArray(array) != 28) {
			mismatches |= 20;
		}
	}
	{
		const int expected = 17;
		(void)expected;
		if (probeNegatedUnknown(widget) != 17) {
			mismatches |= 21;
		}
		if (probeNegatedUnknown(pointer) != 17) {
			mismatches |= 22;
		}
	}
	{
		const int expected = 19;
		(void)expected;
		if (probeDisjunctionUnknown(widget) != 19) {
			mismatches |= 23;
		}
		if (probeDisjunctionUnknown(pointer) != 19) {
			mismatches |= 24;
		}
	}
	{
		const int expected = 21;
		(void)expected;
		if (probeConjunctionUnknown(pointer) != 21) {
			mismatches |= 25;
		}
	}
	if (Holder<int>::pointerMember(pointer) != 40) {
		mismatches |= 26;
	}
	if (Holder<int>::objectMember(widget) != 41) {
		mismatches |= 27;
	}

	(void)plain;
	return mismatches == 0 ? 0 : 1;
}
