// Nested classes under published complete records receive EntityIds owned by
// the enclosing class EntityId, so named type-member schemas can tip-resolve
// Outer::Inner without colliding with namespace-owned class names.
struct Outer {
	struct Inner {
		using type = int;
	};
	short tag;
};

int useOuter(Outer value) {
	Outer::Inner::type count = value.tag;
	return static_cast<int>(count);
}

int main() {
	Outer value{};
	value.tag = 4;
	return useOuter(value) - 4;
}
