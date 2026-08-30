struct AmbiguousSubscript {
	int operator[](long) {
		return 1;
	}

	int operator[](unsigned) {
		return 2;
	}

	int operator[](short) const {
		return 3;
	}
};

int main() {
	AmbiguousSubscript subscriptable;
	return subscriptable[0];
}
