namespace N {
struct S {
	S() {
		_Arg arg;
		arg.value = 40;
		constructed = arg.value;
	}

	int add(int input) {
		_Arg arg;
		arg.value = input;
		return constructed + arg.value;
	}

private:
	union _Arg {
		int value;
	};

	int constructed = 0;
};
}

int main() {
	N::S s;
	return s.add(2) - 42;
}
