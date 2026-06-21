enum class Small : unsigned short {
	Value = 9
};

int bindNullConstRef(const decltype(nullptr)& value) {
	return value == nullptr ? 11 : 0;
}

int bindNullConstRRef(const decltype(nullptr) const&& value) {
	return value == nullptr ? 13 : 0;
}

int main() {
	const decltype(1) one = 1;
	const decltype(one + 41) answer = one + 41;
	if (answer != 42) return 1;

	const __underlying_type(Small) raw = 9;
	if (raw != 9) return 2;

	if (bindNullConstRef(0) != 11) return 3;
	if (bindNullConstRRef(nullptr) != 13) return 4;

	return 0;
}
