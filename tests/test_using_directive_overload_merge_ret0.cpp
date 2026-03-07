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
	using namespace First;
	using namespace Second;
	return select(0);
}
