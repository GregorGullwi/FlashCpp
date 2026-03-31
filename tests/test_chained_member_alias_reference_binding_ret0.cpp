template <typename T>
struct AliasChain {
	typedef T base_type;
	using mid_type = base_type;
	typedef mid_type final_type;
};

struct Payload {
	int value;
};

int bindConstRef(const int& value) {
	return value;
}

int bindMutableRef(int& value) {
	return value + 10;
}

int bindStructRef(Payload& value) {
	return value.value + 20;
}

int bindConstStructRef(const Payload& value) {
	return value.value + 200;
}

int main() {
	AliasChain<int>::final_type scalar = 4;
	AliasChain<Payload>::final_type payload{9};

	if (bindConstRef(scalar) != 4)
		return 1;
	if (bindMutableRef(scalar) != 14)
		return 2;
	if (bindStructRef(payload) != 29)
		return 3;
	if (bindConstStructRef(payload) != 209)
		return 4;

	return 0;
}
