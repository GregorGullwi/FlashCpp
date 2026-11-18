// Member access on (*wrapper)->member and (**wrapper).member must route unary
// * through overload resolution when the wrapper defines operator*.

struct Target {
	int value;
};

struct MemberWrapper {
	Target target{7};
	Target* operator*() { return &target; }
};

struct FreeWrapper {
	Target target{11};
};

Target* operator*(FreeWrapper& wrapper) {
	return &wrapper.target;
}

int main() {
	MemberWrapper member_wrapper;
	if ((*member_wrapper)->value != 7) {
		return 1;
	}
	if ((**member_wrapper).value != 7) {
		return 2;
	}
	FreeWrapper free_wrapper;
	if ((*free_wrapper)->value != 11) {
		return 3;
	}
	return 0;
}
