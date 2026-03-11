// Deferred mixed incomparable operator candidates must remain ambiguous after substitution.

struct Box {
	int value;

	operator int() const {
		return value;
	}
};

int operator+(Box lhs, int rhs) {
	return lhs.value + rhs;
}

int operator+(int lhs, Box rhs) {
	return lhs + rhs.value;
}

template<typename T>
int addTwice(T value) {
	return value + value;
}

int main() {
	Box box{7};
	return addTwice(box);
}