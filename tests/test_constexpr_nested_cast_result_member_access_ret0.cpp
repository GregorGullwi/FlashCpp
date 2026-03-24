struct NestedCastInner {
	int value;
	constexpr NestedCastInner(int v) : value(v) {}
};

struct NestedCastOuter {
	NestedCastInner inner;
	constexpr NestedCastOuter(int v) : inner(v) {}
};

constexpr NestedCastOuter makeNestedCastOuter() {
	return NestedCastOuter(42);
}

constexpr NestedCastOuter nestedCastObj(7);

static_assert(static_cast<const NestedCastOuter&>(makeNestedCastOuter()).inner.value == 42);
static_assert(const_cast<NestedCastOuter&>(static_cast<const NestedCastOuter&>(nestedCastObj)).inner.value == 7);

int main() {
	return 0;
}
