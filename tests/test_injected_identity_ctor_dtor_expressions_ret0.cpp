template <class T>
struct ConstructionExpressions;

template <class T>
struct ConstructionExpressions {
	T value;
	int total;

	ConstructionExpressions(
		int input = sizeof(static_cast<ConstructionExpressions*>(nullptr))) noexcept
		: total(input + sizeof(static_cast<ConstructionExpressions*>(nullptr))) {}

	~ConstructionExpressions() noexcept(
		sizeof(static_cast<ConstructionExpressions*>(nullptr)) == sizeof(void*)) {}
};

int main() {
	ConstructionExpressions<char> small;
	return small.total == 2 * sizeof(void*) ? 0 : 1;
}
