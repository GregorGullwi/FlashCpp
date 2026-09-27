struct Base {
	int value;
};

struct Derived : public Base {};

int selectPointer(Base*) {
	return 10;
}

int selectPointer(Derived*) {
	return 20;
}

int selectPointer(const Base*) {
	return 30;
}

int selectBasePointer(Base*) {
	return 40;
}

int selectBasePointer(const Base*) {
	return 50;
}

int main() {
	Derived value;
	value.value = 7;
	if (selectPointer(&value) != 20) {
		return 1;
	}

	const Derived* const_value = &value;
	if (selectPointer(const_value) != 30) {
		return 2;
	}
	if (selectBasePointer(&value) != 40) {
		return 3;
	}
	return 0;
}
