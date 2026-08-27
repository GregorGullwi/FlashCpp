// C++20 [over.match.oper]: member and non-member candidates participate in a
// single overload set. Identical conversion sequences do not prefer the member.

struct Value {
	int payload;

	Value operator+(const Value& rhs) const {
		return Value{payload + rhs.payload};
	}
};

Value operator+(const Value& lhs, const Value& rhs) {
	return Value{lhs.payload + rhs.payload};
}

int main() {
	Value lhs{20};
	Value rhs{22};
	return (lhs + rhs).payload;
}
