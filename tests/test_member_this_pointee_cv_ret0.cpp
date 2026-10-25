struct Value {
	int classify() { return 1; }
	int classify() const { return 2; }
	int read() const {
		static_assert(__is_same(decltype(this), const Value*));
		return classify();
	}
	int readVolatile() volatile {
		static_assert(__is_same(decltype(this), volatile Value*));
		return 3;
	}
	int readBoth() const volatile {
		static_assert(__is_same(decltype(this), const volatile Value*));
		return 4;
	}
};
int main() {
	const Value value{};
	return value.read() == 2 ? 0 : 1;
}
