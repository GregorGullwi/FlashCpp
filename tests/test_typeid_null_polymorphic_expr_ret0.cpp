struct Base {
	virtual ~Base() {}
};

int main() {
	Base* value = nullptr;
	try {
		(void)typeid(*value);
		return 1;
	} catch (...) {
		return 0;
	}
}
