template<typename Value>
int callAmbiguous(Value& value) {
	return value.pick(1, 2);
}

struct AmbiguousMember {
	int pick(int lhs, double rhs) {
		return lhs + static_cast<int>(rhs);
	}

	int pick(double lhs, int rhs) {
		return static_cast<int>(lhs) + rhs;
	}
};

int main() {
	AmbiguousMember value;
	return callAmbiguous(value);
}
