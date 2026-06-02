template <typename T>
struct TemplateConstThisOverload {
	T value;

	constexpr T pick() { return T{1}; }
	constexpr T pick() const { return value; }

	constexpr T implicitThis() const {
		return pick();
	}

	constexpr T explicitThis() const {
		return this->pick();
	}
};

constexpr int testTemplateImplicitThis() {
	TemplateConstThisOverload<int> value{40};
	return value.implicitThis();
}

constexpr int testTemplateExplicitThis() {
	TemplateConstThisOverload<int> value{50};
	return value.explicitThis();
}

static_assert(testTemplateImplicitThis() == 40);
static_assert(testTemplateExplicitThis() == 50);

int main() {
	if (testTemplateImplicitThis() != 40) return 1;
	if (testTemplateExplicitThis() != 50) return 2;
	return 0;
}
