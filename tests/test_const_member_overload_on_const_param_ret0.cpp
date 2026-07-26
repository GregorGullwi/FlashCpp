// Regression: const/non-const member overloads must resolve unambiguously when
// the receiver is a const-qualified parameter of a member-function template defined
// on a partial class specialization (tuple::_Get_rest pattern).
template<class... Rest>
struct Node;

template<class T>
struct Node<T> {
	T value{};
};

template<class T, class... Rest>
struct Node<T, Rest...> : Node<Rest...> {
	T head{};

	constexpr Node<Rest...>& rest() noexcept { return *this; }
	constexpr const Node<Rest...>& rest() const noexcept { return *this; }

	template<class U, class... Others>
	constexpr void touch(const Node<U, Others...>& other) const {
		(void)other.rest();
	}
};

int main() {
	Node<int, float> node;
	Node<int, float> other;
	node.touch(other);
	return 0;
}
