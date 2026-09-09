// Concrete type parameter packs on published namespace/global class templates
// must stamp an ordered canonical TemplateSpecialization identity. Dependent
// expansions remain deliberately outside this 3A slice.
template <typename... Types>
struct Pack {
	static constexpr int count = sizeof...(Types);
};

template <typename First, typename... Rest>
struct WithPrefix {
	static constexpr int count = 1 + sizeof...(Rest);
};

int plainCount(Pack<int, char, double>*) {
	return Pack<int, char, double>::count;
}

int prefixedCount(WithPrefix<long, short, bool>*) {
	return WithPrefix<long, short, bool>::count;
}

int main() {
	return plainCount(nullptr) + prefixedCount(nullptr) - 6;
}
