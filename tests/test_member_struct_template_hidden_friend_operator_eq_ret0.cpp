template <class View>
struct outer {
	template <bool Const>
	struct iterator {
		int value;

		friend constexpr bool operator==(const iterator& left, const iterator& right) {
			return left.value == right.value;
		}
	};
};

int main() {
	outer<int>::iterator<true>* it = nullptr;
	return it == nullptr ? 0 : 1;
}
