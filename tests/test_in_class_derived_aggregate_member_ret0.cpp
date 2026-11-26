struct Base { int value; };
struct Derived : Base { int extra; };
struct Holder { Derived storage{11, 12}; };

struct PairBase { int first; int second; };
struct TwoMemberDerived : PairBase { int tail; };
struct TwoMemberHolder { TwoMemberDerived storage{51, 52, 53}; };
struct TwoMemberBracedHolder { TwoMemberDerived storage{{54, 55}, 56}; };

struct Padding { long long guard; };
struct Left { short first; };
struct Right { long long second; };
struct MultiDerived : Padding, Left, Right { char tail; };
struct MultiHolder { MultiDerived storage{41, 42, 43, 44}; };

struct Outer { Holder inner; };

struct ExplicitHolder { Derived storage; ExplicitHolder() : storage{15, 16} {} };

struct Pair { int a; int b; };
struct PairHolder { Pair pair{31, 32}; };

Derived global_derived{21, 22};
MultiDerived global_multi{71, 72, 73, 74};

int main() {
	Holder default_holder;
	if (default_holder.storage.value != 11) return 1;
	if (default_holder.storage.extra != 12) return 2;

	Holder value_holder{};
	if (value_holder.storage.value != 11) return 3;
	if (value_holder.storage.extra != 12) return 4;

	Derived local{21, 22};
	if (local.value != 21 || local.extra != 22) return 5;

	TwoMemberHolder two_member;
	if (two_member.storage.first != 51 || two_member.storage.second != 52 || two_member.storage.tail != 53) return 6;

	TwoMemberBracedHolder two_member_braced;
	if (two_member_braced.storage.first != 54 || two_member_braced.storage.second != 55 ||
		two_member_braced.storage.tail != 56) {
		return 7;
	}

	MultiHolder multi;
	if (multi.storage.guard != 41 || multi.storage.first != 42 || multi.storage.second != 43 || multi.storage.tail != 44) {
		return 8;
	}

	Outer outer_default;
	if (outer_default.inner.storage.value != 11 || outer_default.inner.storage.extra != 12) return 9;

	Outer outer_value{};
	if (outer_value.inner.storage.value != 11 || outer_value.inner.storage.extra != 12) return 10;

	PairHolder pair_holder;
	if (pair_holder.pair.a != 31 || pair_holder.pair.b != 32) return 11;

	Pair local_pair{81, 82};
	if (local_pair.a != 81 || local_pair.b != 82) return 12;

	if (global_derived.value != 21 || global_derived.extra != 22) return 13;
	if (global_multi.guard != 71 || global_multi.first != 72 || global_multi.second != 73 || global_multi.tail != 74) return 14;

	ExplicitHolder explicit_holder;
	if (explicit_holder.storage.value != 15 || explicit_holder.storage.extra != 16) return 15;

	return 0;
}
