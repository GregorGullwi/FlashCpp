// Explicit derived-to-base casts from an intermediate base of a virtually
// inherited diamond must consume the published virtual-base ABI index.

struct Root {
	long long value;
};

struct Left : virtual Root {
	int left;
};

struct Right : virtual Root {
	int right;
};

struct Diamond : Left, Right {
	short tail;
};

int main() {
	Diamond diamond{};
	diamond.value = 41;
	diamond.left = 3;
	diamond.right = 5;
	diamond.tail = 7;
	Left* left = &diamond;
	Right* right = &diamond;
	Root* left_root = static_cast<Root*>(left);
	Root* right_root = static_cast<Root*>(right);
	if (left_root != right_root)
		return 1;
	if (left_root->value != 41)
		return 2;
	if (right_root->value != 41)
		return 3;
	return 0;
}
