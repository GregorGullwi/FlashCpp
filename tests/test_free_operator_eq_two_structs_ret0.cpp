struct Left {
	int value;
};

struct Right {
	int value;
};

bool operator==(const Left& lhs, const Right& rhs) {
	return lhs.value == rhs.value;
}

int main() {
	Left lhs{42};
	Right rhs{42};
	Right other{7};

	if (!(lhs == rhs)) {
		return 1;
	}
	if (lhs == other) {
		return 2;
	}
	return 0;
}
