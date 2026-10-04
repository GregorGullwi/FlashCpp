struct Owner {
	template <class Type>
	int pointer(Type*) const & {
		return 42;
	}

	template <class Type>
	int pointer(Type) const & {
		return 1;
	}

};

using PointerTarget = int (Owner::*)(int*) const &;

int main() {
	PointerTarget pointer = static_cast<PointerTarget>(&Owner::pointer);
	return pointer == nullptr;
}
