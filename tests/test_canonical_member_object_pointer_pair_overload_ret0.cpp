struct Owner {
	int integer;
	char character;
};

struct OtherOwner {
	int integer;
};

struct Base {
	int value;
};

struct Derived : Base {
	int own;
};

int choose(int Owner::*) {
	return 1;
}

int choose(char Owner::*) {
	return 2;
}

int choose(int OtherOwner::*) {
	return 3;
}

int choose(int Base::*) {
	return 4;
}

int choose(int Derived::*) {
	return 5;
}

int main() {
	int Owner::* integer = &Owner::integer;
	char Owner::* character = &Owner::character;
	int OtherOwner::* other = &OtherOwner::integer;
	int Base::* base = &Base::value;
	if (choose(integer) != 1)
		return 1;
	if (choose(character) != 2)
		return 2;
	if (choose(other) != 3)
		return 3;
	if (choose(base) != 4)
		return 4;
	return 0;
}
