// Focused migration-counter test: these argument conversions still leave the
// canonical planner and are planned by the syntax compatibility tail
// (converting-constructor argument and derived-to-base pointer argument), so
// canonical_conversion_compatibility_fallback must stay non-zero here.

struct Target {
	int value;
	Target(int v) : value(v) {}
};

struct Base {
	int value;
};

struct Derived : Base {
	Derived() {
		value = 2;
	}
};

static int acceptTarget(Target target) {
	return target.value;
}

static int acceptBase(Base* base) {
	return base->value;
}

int main() {
	Derived derived;
	return acceptTarget(4) + acceptBase(&derived);
}
