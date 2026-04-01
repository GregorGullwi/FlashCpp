struct Value {
	int data;

	friend auto operator<=>(const Value& lhs, const Value& rhs) = default;
};

int main() {
	Value low{1};
	Value same{1};
	Value high{3};

	if (!((low <=> same) == 0)) {
		return 1;
	}
	if (!((low <=> high) < 0)) {
		return 2;
	}
	if (!((high <=> low) > 0)) {
		return 3;
	}
	return 0;
}
