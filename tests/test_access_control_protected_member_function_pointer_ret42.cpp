class Base {
protected:
	int read() const { return 7; }
};

class Derived : public Base {
public:
	int test() const {
		int (Derived::*member)() const = &Derived::read;
		(void)member;
		return 42;
	}
};

int main() {
	Derived value;
	return value.test();
}
