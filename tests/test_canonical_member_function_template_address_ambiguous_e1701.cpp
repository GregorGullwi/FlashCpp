template <class Type>
struct ResultBox {
};

struct Owner {
	template <class Left, class Right>
	int run(Left*, Right) const & {
		return 1;
	}

	template <class Left, class Right>
	int run(Left, ResultBox<Right>*) const & {
		return 2;
	}
};

using Target = int (Owner::*)(ResultBox<int>*, ResultBox<int>*) const &;

int main() {
	Target member = static_cast<Target>(&Owner::run);
	return member == nullptr;
}
