namespace outer::inline v1::inline abi {
	struct DeepType {
		int value;
	};

	int deep_value(DeepType value) {
		return value.value;
	}
}

int main() {
	outer::DeepType from_outer{7};
	if (from_outer.value != 7) return 1;
	if (outer::deep_value(from_outer) != 7) return 2;

	outer::v1::DeepType from_mid{8};
	if (from_mid.value != 8) return 3;
	if (outer::v1::deep_value(from_mid) != 8) return 4;

	return 0;
}
