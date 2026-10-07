struct Base {
	int value;
};

struct Derived : Base {};

struct Source {
	Derived derived;
	operator Derived&() { return derived; }
};

int select_base(Base& value) {
	value.value = 57;
	return 0;
}

long select_base(...) {
	return 1;
}

int main() {
	Source source;
	return select_base(source) == 0 && source.derived.value == 57 ? 0 : 1;
}
