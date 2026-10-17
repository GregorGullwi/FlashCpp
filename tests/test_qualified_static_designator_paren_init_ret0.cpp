// Parenthesized direct initialization must keep a bare qualified non-type
// (static function, static data member, enumerator) as an expression.
// Treating that qualified-id as a parameter type parses the variable away.

struct Source {
	int tag;
	using Value = int;
	static int answer() { return 7; }
	static int count;
	static int make() { return 29; }
	enum { Green = 3 };
	enum class Channel { Blue = 5 };
	struct Inner {
		static int answer() { return 11; }
	};
	Source() : tag(0) {}
};

int Source::count = 13;

struct Base {
	static int inherited() { return 17; }
};

struct Derived : Base {};

struct FunctionReceiver {
	int value;
	explicit FunctionReceiver(int (*argument)()) : value(argument()) {}
};

namespace Tools {
	struct Source {
		static int answer() { return 23; }
	};
}

using Alias = Source;

struct AmbiguousLeft { using Value = int; };
struct AmbiguousRight { static int Value() { return 4; } };
struct AmbiguousDerived : AmbiguousLeft, AmbiguousRight {};
struct AmbiguousDerivedSwapped : AmbiguousRight, AmbiguousLeft {};

struct HiddenBase { using Value = int; };
struct HidingMid : HiddenBase { static int Value() { return 37; } };
struct HidingDerived : HidingMid, HiddenBase {};

struct DiamondCommon { static int inherited() { return 41; } };
struct DiamondLeft : virtual DiamondCommon {};
struct DiamondRight : virtual DiamondCommon {};
struct DiamondDerived : DiamondLeft, DiamondRight {};

struct OnlyTypeBase { using Value = int; };
struct OtherBase { static int other() { return 1; } };
struct OnlyTypeDerived : OnlyTypeBase, OtherBase {};

int pass(Source::Value value) { return value; }

int accept(Source object) { return object.tag; }

int take(OnlyTypeDerived::Value value) { return value; }

int nested_type_still_declares_a_function() {
	int pass(Source::Value);
	return pass(19);
}

int injected_class_name_still_declares_a_function() {
	int accept(Source::Source);
	Source object;
	object.tag = 31;
	return accept(object);
}

int only_type_base_still_declares_a_function() {
	int take(OnlyTypeDerived::Value);
	return take(43);
}

int main() {
	FunctionReceiver receiver(Source::answer);
	if (receiver.value != 7) return 1;
	FunctionReceiver nested(Source::Inner::answer);
	if (nested.value != 11) return 2;
	FunctionReceiver inherited(Derived::inherited);
	if (inherited.value != 17) return 3;
	FunctionReceiver aliased(Alias::answer);
	if (aliased.value != 7) return 4;
	FunctionReceiver namespaced(Tools::Source::answer);
	if (namespaced.value != 23) return 5;
	int stored(Source::count);
	if (stored != 13) return 6;
	int green(Source::Green);
	if (green != 3) return 7;
	int blue(Source::Channel::Blue);
	if (blue != 5) return 8;
	int made(Source::make());
	if (made != 29) return 9;
	FunctionReceiver braced{Source::answer};
	if (braced.value != 7) return 10;
	if (nested_type_still_declares_a_function() != 19) return 11;
	if (injected_class_name_still_declares_a_function() != 31) return 12;
	FunctionReceiver ambiguous_left(AmbiguousDerived::Value);
	if (ambiguous_left.value != 4) return 13;
	FunctionReceiver ambiguous_right(AmbiguousDerivedSwapped::Value);
	if (ambiguous_right.value != 4) return 14;
	FunctionReceiver dominating(HidingDerived::Value);
	if (dominating.value != 37) return 15;
	FunctionReceiver diamond(DiamondDerived::inherited);
	if (diamond.value != 41) return 16;
	if (only_type_base_still_declares_a_function() != 43) return 17;
	return 0;
}
