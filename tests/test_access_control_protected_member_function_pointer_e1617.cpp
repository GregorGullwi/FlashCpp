class Base {
protected:
	int read() const { return 42; }
};

class Derived : public Base {
public:
	auto getBasePointer() const { return &Base::read; }
};

int main() {
	Derived value;
	(void)value;
	return 0;
}
