// Regression: constructor-template specializations with identical conversion
// sequences must be ordered from their source template parameter patterns.
struct DelegateTag {};

template <typename... Stored>
struct Product {
	int selected;

	template <typename First, typename... Rest>
	Product(First&& first, Rest&&... rest)
		: Product(DelegateTag{}, first, rest...) {}

	template <typename Tag, typename First, typename... Rest>
	Product(Tag, First&&, Rest&&...)
		: selected(1) {}
};

int main() {
	Product<int, float, double> product(1, 2.0f, 3.0);
	return product.selected == 1 ? 0 : 1;
}
