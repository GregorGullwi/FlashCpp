template <class View>
struct outer {
	template <bool Const>
	struct iterator {
		int current;
		outer* parent;

		constexpr iterator(outer& parent_, int current_) noexcept(true)
			: current{current_}, parent{&parent_} {
			current = current + 1;
		}
	};
};

int main() {
	outer<int>::iterator<true>* it = nullptr;
	return it == nullptr ? 0 : 1;
}
