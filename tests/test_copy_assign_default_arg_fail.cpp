struct CopyAssignWithDefaultArg {
	int value;

	CopyAssignWithDefaultArg& operator=(const CopyAssignWithDefaultArg& other, int bias = 40) {
		value = other.value + bias;
		return *this;
	}
};

int main() {
	CopyAssignWithDefaultArg lhs{1};
	CopyAssignWithDefaultArg rhs{2};
	lhs = rhs;
	return lhs.value;
}
