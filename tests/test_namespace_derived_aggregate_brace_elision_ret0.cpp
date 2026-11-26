// A namespace-scope brace-elided aggregate initializer must give a base
// subobject every clause its own aggregate leaves need before advancing to the
// next member. Treating each base as one clause left the base members zero and
// shifted the following clauses onto the derived member.

struct TwoPartBase {
	int first;
	int second;
};

struct ElidedDerived : TwoPartBase {
	int tail;
};

struct ExplicitDerived : TwoPartBase {
	int tail;
};

struct FlatAggregate {
	int first;
	int second;
	int tail;
};

struct OnePartBase {
	int value;
};

struct OnePartDerived : OnePartBase {
	int tail;
};

struct Holder {
	ElidedDerived storage{31, 32, 33};
};

ElidedDerived elided{1, 2, 3};
ExplicitDerived explicit_form{{4, 5}, 6};
FlatAggregate flat{7, 8, 9};
OnePartDerived one_part{10, 11};

int main() {
	if (elided.first != 1) return 1;
	if (elided.second != 2) return 2;
	if (elided.tail != 3) return 3;
	if (explicit_form.first != 4) return 4;
	if (explicit_form.second != 5) return 5;
	if (explicit_form.tail != 6) return 6;
	if (flat.first != 7) return 7;
	if (flat.second != 8) return 8;
	if (flat.tail != 9) return 9;
	if (one_part.value != 10) return 10;
	if (one_part.tail != 11) return 11;

	ElidedDerived local{12, 13, 14};
	if (local.first != 12) return 12;
	if (local.second != 13) return 13;
	if (local.tail != 14) return 14;

	Holder holder;
	if (holder.storage.first != 31) return 15;
	if (holder.storage.second != 32) return 16;
	if (holder.storage.tail != 33) return 17;
	return 0;
}
