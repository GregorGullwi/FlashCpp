struct NonTemplateLess {
	constexpr auto operator()(int lhs, int rhs) const noexcept(true) -> decltype(lhs < rhs) {
		return lhs < rhs;
	}
};

template <typename T>
struct TemplateLess {
	constexpr auto operator()(const T& lhs, const T& rhs) const noexcept(true) -> decltype(lhs < rhs) {
		return lhs < rhs;
	}
};

int main() {
	NonTemplateLess non_template_less;
	TemplateLess<long> template_less;
	return (non_template_less(1, 2) && template_less(2L, 3L)) ? 0 : 1;
}
