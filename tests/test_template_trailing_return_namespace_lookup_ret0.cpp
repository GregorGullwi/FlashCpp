namespace lookup_ns {
	struct Box {
		int value;
	};

	Box makeBox() {
		return Box{42};
	}

	template <class T>
	auto makeTyped(T) -> decltype(makeBox()) {
		return makeBox();
	}
}

int main() {
	lookup_ns::Box box = lookup_ns::makeTyped(0);
	return box.value == 42 ? 0 : 1;
}
