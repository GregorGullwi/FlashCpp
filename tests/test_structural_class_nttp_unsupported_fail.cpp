struct Key {
	int value;
};

template <Key K>
struct structural_tag {
	static constexpr int value = K.value;
};

constexpr Key key{42};

int main() {
	return structural_tag<key>::value;
}
