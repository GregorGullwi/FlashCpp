namespace ns {
struct Box {
	int value;

	Box(int v) : value(v) {}
	int get() const { return value; }
};
}

struct Outer {
	struct Inner {
		int value;

		Inner(int v) : value(v) {}
		int get() const { return value; }
	};
};

int main() {
	ns::Box a{40};
	Outer::Inner b(2);
	ns::Box c = ns::Box(3);
	Outer::Inner d{4};
	return (a.get() + b.get() == 42 && c.get() == 3 && d.get() == 4) ? 0 : 1;
}
