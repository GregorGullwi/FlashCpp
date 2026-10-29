struct Base { int value; };

struct Source {
	explicit operator Base() const { return Base{7}; }
};

struct Derived : Base { int tail; };

int main() {
	Derived value{Source{}, 3};
	return value.tail;
}
