struct Base {
	int value;
};

struct Derived : public Base {};

int selectReference(const Base& value) {
	return value.value + 10;
}

int selectReference(const Derived& value) {
	return value.value + 20;
}

int selectReference(Base&& value) {
	return value.value + 30;
}

int selectReference(Derived&& value) {
	return value.value + 40;
}

int main() {
	Derived value;
	value.value = 2;
	if (selectReference(value) != 22) {
		return 1;
	}
	if (selectReference(static_cast<Derived&&>(value)) != 42) {
		return 2;
	}
	return 0;
}
