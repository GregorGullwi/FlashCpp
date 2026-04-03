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

static_assert(Base<int, 9>::payload.a == 9);
static_assert(Base<int, 9>::payload.b == 4);
static_assert(Derived::payload.a == 9);
static_assert(Derived::payload.b == 4);

int main() {
	Derived d{};
	return d.value;
}
