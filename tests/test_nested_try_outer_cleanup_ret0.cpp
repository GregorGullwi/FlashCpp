int destroyed = 0;

struct Tracked {
	int id;

	explicit Tracked(int value) : id(value) {}
	~Tracked() {
		destroyed = destroyed * 10 + id;
	}
};

int main() {
	try {
		Tracked a(1);
		try {
			throw 7;
		} catch (...) {
		}

		Tracked b(2);
		throw 42;
	} catch (int value) {
		if (value != 42) {
			return 1;
		}
		if (destroyed != 21) {
			return 2;
		}
		return 0;
	} catch (...) {
		return 3;
	}

	return 4;
}