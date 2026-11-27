// [expr.mptr.oper]: the left operand of `.*` designates the object that the
// member offset applies to. When that object is itself a member-access result,
// the receiver must carry a real address, so read and write through
// `object.middle.leaf.*pointer` target the subobject for local and namespace
// scope receivers alike. The controls cover the neighbouring lvalue forms:
// direct member access, an array member, a reference member, and `object.*p`
// on a direct object.

struct Leaf {
	int value;
	int tail;
};

struct Middle {
	int before;
	Leaf leaf;
};

struct Outer {
	int prefix;
	Middle middle;
	Leaf direct;
};

struct LeafHolder {
	int Leaf::*pointer;
};

struct OuterHolder {
	int Outer::*pointer;
};

struct ArrayCarrier {
	int values[3];
};

struct ReferenceCarrier {
	int& target;
};

Outer global;

int main() {
	Outer object{};
	object.prefix = 1;
	object.middle.before = 2;
	object.middle.leaf.value = 7;
	object.middle.leaf.tail = 11;
	object.direct.value = 3;
	object.direct.tail = 5;

	LeafHolder value_holder{&Leaf::value};
	LeafHolder tail_holder{&Leaf::tail};

	// Read through a single-level member receiver.
	if ((object.direct.*value_holder.pointer) != 3) {
		return 1;
	}
	// Write through a single-level member receiver.
	(object.direct.*value_holder.pointer) = 13;
	if (object.direct.value != 13) {
		return 2;
	}

	// Read through a nested member receiver.
	if ((object.middle.leaf.*value_holder.pointer) != 7) {
		return 3;
	}
	// Write through a nested member receiver.
	(object.middle.leaf.*value_holder.pointer) = 9;
	if (object.middle.leaf.value != 9) {
		return 4;
	}

	// A non-primary member offset must select tail, not value.
	if ((object.middle.leaf.*tail_holder.pointer) != 11) {
		return 5;
	}
	(object.middle.leaf.*tail_holder.pointer) = 15;
	if (object.middle.leaf.tail != 15) {
		return 6;
	}

	// The same offsets apply to a namespace-scope receiver object.
	global.middle.leaf.value = 17;
	global.middle.leaf.tail = 19;
	if ((global.middle.leaf.*value_holder.pointer) != 17) {
		return 7;
	}
	(global.middle.leaf.*tail_holder.pointer) = 21;
	if (global.middle.leaf.tail != 21) {
		return 8;
	}

	// Control: direct member access.
	if (object.middle.leaf.value != 9) {
		return 9;
	}

	// Control: an array member reaches its own storage.
	ArrayCarrier arrays{};
	arrays.values[1] = 23;
	if (arrays.values[1] != 23) {
		return 10;
	}

	// Control: a reference member reaches the referenced object.
	int referenced = 29;
	ReferenceCarrier carrier{referenced};
	if (carrier.target != 29) {
		return 11;
	}
	carrier.target = 31;
	if (referenced != 31) {
		return 12;
	}

	// Control: `object.*pointer` on a direct object.
	OuterHolder prefix_holder{&Outer::prefix};
	if ((object.*prefix_holder.pointer) != 1) {
		return 13;
	}
	(object.*prefix_holder.pointer) = 37;
	if (object.prefix != 37) {
		return 14;
	}

	return 42;
}
