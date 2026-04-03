struct Base {
	virtual int get() { return 42; }
	virtual ~Base() {}
};

int main() {
	Base base;
	Base* source = &base;
	Base* rebound = dynamic_cast<Base*>(source);
	Base* copied = rebound;
	return copied ? 0 : 1;
}
