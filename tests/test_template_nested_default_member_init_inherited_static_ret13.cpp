struct Base {
	struct Payload {
		int a;
		int b;
	};

	static constexpr Payload payload = {9, 4};
};

struct Derived : Base {
	int value = payload.a + payload.b;
};

int main() {
	Derived d;
	return d.value;
}
