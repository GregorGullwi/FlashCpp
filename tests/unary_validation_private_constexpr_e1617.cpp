class Value {
	constexpr int operator-() const { return 7; }
};
static_assert(-Value{} == 7);
