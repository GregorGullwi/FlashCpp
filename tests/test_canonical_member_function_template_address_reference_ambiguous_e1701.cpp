struct Owner {
	template <class Type>
	int run(Type&) const & {
		return 1;
	}

	template <class Type>
	int run(Type) const & {
		return 2;
	}
};

using Target = int (Owner::*)(int&) const &;

int main() {
	Target member = static_cast<Target>(&Owner::run);
	return member == nullptr;
}
