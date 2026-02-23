namespace std {
enum memory_order { memory_order_relaxed = 7 };

struct A {
	static int g() { return memory_order_relaxed; }
};
}

int main() {
	return std::A::g() - 7;
}
