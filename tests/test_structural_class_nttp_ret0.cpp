struct Key {
	int value;
};

template <Key K>
struct structural_tag {};

constexpr Key key{42};

int main() {
	return sizeof(structural_tag<key>) > 0 ? 0 : 1;
}
