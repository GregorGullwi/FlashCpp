struct Base {
	virtual ~Base() {}
};

template <typename T>
struct Box {
	static Base* helperBasePtr() {
		static Base value;
		return &value;
	}

	static int direct() {
		return helperBasePtr() ? 42 : 0;
	}

	static int casted() {
		return dynamic_cast<Base*>(helperBasePtr()) ? 42 : 0;
	}
};

int main() {
	if (Box<int>::direct() != 42) {
		return 1;
	}
	if (Box<int>::casted() != 42) {
		return 2;
	}
	return 0;
}
