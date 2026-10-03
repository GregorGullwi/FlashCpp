template <class Type>
struct Owner {
	template <class Value>
	Value run(Value value) const & {
		return value;
	}
};

int choose(int (Owner<int>::*)(double) const &) {
	return 0;
}

template <class Type>
auto selectInconsistentMemberTemplateAddress() {
	return choose(&Owner<Type>::run);
}

int main() {
	return selectInconsistentMemberTemplateAddress<int>();
}
