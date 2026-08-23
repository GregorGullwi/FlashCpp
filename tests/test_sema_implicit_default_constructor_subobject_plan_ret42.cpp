struct LeftBase {
	int left = 7;
};

struct RightBase {
	int right = 11;
};

template<typename T>
struct MultipleBases : LeftBase, RightBase {
	T value = 3;
};

struct ArrayElement {
	int value = 21;
};

template<typename T>
struct ArrayDefaultMemberInitializer {
	ArrayElement values[2]{};
};

template<typename T>
struct ScalarDefaultMemberInitializer {
	ArrayElement value{};
};

int main() {
	MultipleBases<int> bases;
	ArrayDefaultMemberInitializer<int> array;
	ScalarDefaultMemberInitializer<int> scalar;
	if (bases.left != 7 || bases.right != 11 || bases.value != 3 ||
		array.values[0].value != 21 || array.values[1].value != 21 || scalar.value.value != 21) {
		return 1;
	}
	return 42;
}
