struct MyException {
	int value = 42;
	virtual ~MyException() {}
};

int main() {
	int sum = 0;

	try {
		throw MyException{};
	} catch (MyException& e) {
		sum += (e.value == 42) ? 1 : 100;
	}

	try {
		throw MyException{};
	} catch (MyException& e) {
		sum += (e.value == 42) ? 2 : 100;
	}

	return sum == 3 ? 0 : sum;
}