// A direct member function template inside a published class template owns a
// distinct TemplateDeclId, even though the enclosing class has no EntityId
// while its body parses.
template <typename Outer>
struct TemplateFunctionOwner {
	template <typename Left, typename Right>
	Right select(Left left, Right right) {
		return static_cast<Right>(left + right);
	}
};

int main() {
	TemplateFunctionOwner<int> owner;
	return owner.select(19, 23) - 42;
}
