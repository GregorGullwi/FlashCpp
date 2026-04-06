struct Base {
	int value;

	Base(double v) : value(static_cast<int>(v * 2.0)) {}
};

struct DerivedFromBase : Base {
	DerivedFromBase(int v) : Base(v) {}
};

struct Delegating {
	int value;

	Delegating(int v) : Delegating(v, 2.0) {}
	Delegating(double base, double factor) : value(static_cast<int>(base * factor)) {}
};

int main() {
	DerivedFromBase base(21);
	Delegating delegating(21);
	return base.value + delegating.value;
}
