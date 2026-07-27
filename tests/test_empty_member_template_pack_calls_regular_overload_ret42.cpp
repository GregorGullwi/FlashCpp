struct Delta {
	int value;
};

struct Picker {
	int pick(short value, const Delta& delta) const {
		return value + delta.value;
	}

	template <typename... Types>
	int pick(short value) const {
		Delta delta{35};
		return this->pick(value, delta) + sizeof...(Types);
	}
};

int main() {
	Picker picker;
	return picker.pick(7) == 42 ? 42 : 0;
}
