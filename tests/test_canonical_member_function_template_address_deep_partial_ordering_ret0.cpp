// Sixteen nested template arguments stress ordering below the parser's depth-20 limit.
template <class Type>
struct Box {
};

struct Owner {
	template <class Type>
	int run(Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Type>>>>>>>>>>>>>>>>*) const & {
		return 42;
	}

	template <class Type>
	int run(Type*) const & {
		return 1;
	}
};

using Target = int (Owner::*)(Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<Box<int>>>>>>>>>>>>>>>>*) const &;

int main() {
	Target selected = static_cast<Target>(&Owner::run);
	return selected == nullptr;
}
