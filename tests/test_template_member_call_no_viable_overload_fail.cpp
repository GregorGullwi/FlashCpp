template<typename Value>
int callMissing(Value& value) {
	return value.pick("bad");
}

struct IntOnlyMember {
	int pick(int value) {
		return value + 1;
	}
};

int main() {
	IntOnlyMember value;
	return callMissing(value);
}
