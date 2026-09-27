struct Owner {
	int noThrow(int value) {
		return value;
	}

	int narrow(char value) {
		return value;
	}
};

struct OtherOwner {
	int noThrow(int value) {
		return value;
	}
};

int choose(int (Owner::*)(int)) {
	return 1;
}

int choose(int (Owner::*)(char)) {
	return 2;
}

int choose(int (OtherOwner::*)(int)) {
	return 3;
}

int main() {
	int (Owner::*integer_member)(int) = &Owner::noThrow;
	int (Owner::*char_member)(char) = &Owner::narrow;
	int (OtherOwner::*other_member)(int) = &OtherOwner::noThrow;
	if (choose(integer_member) != 1)
		return 1;
	if (choose(char_member) != 2)
		return 2;
	if (choose(other_member) != 3)
		return 3;
	return 0;
}
