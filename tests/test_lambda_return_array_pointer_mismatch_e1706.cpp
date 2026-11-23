// An array operand and a pointer operand are different return types. The array
// decays for the value, but the declared array object itself is not the pointer
// type, so the deduced return type is inconsistent.

int row[3] = {1, 2, 3};

int use_array(int flag) {
	auto select = [&]() -> decltype(auto) {
		if (flag) {
			return row;
		}
		return &row[0];
	};
	return select()[0];
}

int main() {
	return use_array(1) - 1;
}
