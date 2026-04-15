struct SetterInCtor {
	int value;

	constexpr void setValue(int input) {
		value = input;
	}

	constexpr SetterInCtor(int input) {
		setValue(input);
	}
};

constexpr SetterInCtor globalSetter{42};
static_assert(globalSetter.value == 42);

int main() {
	SetterInCtor localSetter{7};
	return (globalSetter.value == 42 && localSetter.value == 7) ? 0 : 1;
}
