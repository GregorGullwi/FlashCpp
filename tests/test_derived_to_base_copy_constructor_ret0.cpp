// Derived-to-base object initialization must invoke the selected Base
// copy/move constructor on the correctly adjusted base subobject.

struct Prefix {
	int marker;
};

struct Base {
	int value;

	Base() : value(0) {}
	Base(const Base& other) : value(other.value + 100) {}
};

struct Derived : Prefix, Base {
	int tail;

	Derived(int input) : Base(), tail(input) {
		marker = 7;
		value = input;
	}
};

int take_base_by_value(Base value) {
	return value.value;
}

Base return_base(Derived& source) {
	return source;
}

struct BaseReferenceHolder {
	int value;

	BaseReferenceHolder(const Base& source) : value(source.value + 1) {}
};

int main() {
	Derived source(7);
	Base initialized = source;
	if (initialized.value != 107) {
		return 1;
	}
	if (take_base_by_value(source) != 107) {
		return 2;
	}
	Base returned = return_base(source);
	if (returned.value != 107) {
		return 3;
	}
	BaseReferenceHolder holder(source);
	return holder.value == 8 ? 0 : 4;
}
