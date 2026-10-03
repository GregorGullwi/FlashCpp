template <class Type>
struct Owner {
	Type run(Type value) const & {
		return value;
	}
};

int choose(int (Owner<int>::*)(double) const &);

template <class Type>
auto selectMismatchedMemberAddress() {
	return choose(&Owner<Type>::run);
}

static_assert(sizeof(decltype(selectMismatchedMemberAddress<int>())) ==
	sizeof(int));

int main() {
	return 0;
}
