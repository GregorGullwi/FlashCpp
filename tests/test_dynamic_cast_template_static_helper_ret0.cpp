struct Base {
	virtual ~Base() {}
};

struct PayloadBase {
	int value;
	virtual ~PayloadBase() {}
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

template <typename T>
struct PayloadBox {
	static PayloadBase* helperBasePtr() {
		static PayloadBase value;
		return &value;
	}

	static int casted() {
		PayloadBase* rebound = dynamic_cast<PayloadBase*>(helperBasePtr());
		return rebound ? 42 : 0;
	}
};

int main() {
	if (Box<int>::direct() != 42) {
		return 1;
	}
	if (Box<int>::casted() != 42) {
		return 2;
	}
	if (PayloadBox<int>::casted() != 42) {
		return 3;
	}
	return 0;
}
