constexpr auto make_mul3() {
	return [](int x) { return x * 3; };
}

constexpr auto make_add(int delta) {
	return [delta](int x) { return x + delta; };
}

constexpr auto mul3 = make_mul3();
constexpr auto add7 = make_add(7);

static_assert(mul3(5) == 15);
static_assert(add7(8) == 15);

int main() {
	return (mul3(4) == 12 && add7(5) == 12) ? 0 : 1;
}
