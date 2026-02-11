// Test: namespace-qualified friend struct declaration in template context
// Pattern: template<typename, typename> friend struct ns::ClassName;

namespace ns {
template<typename T, typename U>
struct Helper {};
}

template<typename T>
struct Container {
	template<typename, typename>
	friend struct ns::Helper;

	int value;
};

int main() {
	Container<int> c;
	c.value = 3;
	return c.value;
}
