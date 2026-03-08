namespace First {
	int select(double) {
		return 1;
	}
}

namespace Second {
	int select(int) {
		return 0;
	}
}

int main() {
	using Second::select;
	using First::select;
	return select(0);
}
