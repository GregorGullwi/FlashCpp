// Target return type must disambiguate member-function-template addresses
// whose parameter patterns both match the target parameter list.
template <class Type>
struct ResultBox {
	Type value;
};

struct Owner {
	template <class Type>
	Type* run(const Type*) const & {
		return nullptr;
	}

	template <class Type>
	ResultBox<Type> run(Type* value) const & {
		return {*value};
	}
};

using BoxRun = ResultBox<const int> (Owner::*)(const int*) const &;

int choose(BoxRun) {
	return 0;
}

int choose(...) {
	return 1;
}

int main() {
	return choose(&Owner::run);
}
