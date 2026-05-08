struct Picker {
	int pick(int, int) const {
		return 42;
	}

	template<typename... Ts>
	int pick(int value) const {
		return this->pick(value, 2);
	}
};

int main() {
	Picker picker;
	return picker.pick(40) == 42 ? 0 : 1;
}
