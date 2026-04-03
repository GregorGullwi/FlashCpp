template <typename T, int N>
struct Base {
	struct Payload {
		int a;
		int b;
	};

	static constexpr Payload payload = { N, int(sizeof(T)) };
};

struct Derived : Base<int, 9> {
	int value = payload.a + payload.b;
};

int main() {
	Derived d{};
	return d.value;
}
