struct Base {
	int value;

	Base(long long v) : value(static_cast<int>(v)) {}
};

struct DerivedFromBase : Base {
	DerivedFromBase(int v) : Base(v + 21LL) {}
};

struct Delegating {
	int value;

	Delegating(int v) : Delegating(v + 21LL) {}
	Delegating(long long v) : value(static_cast<int>(v)) {}
};

int main() {
	DerivedFromBase base(21);
	Delegating delegating(21);
	return base.value + delegating.value;
}
