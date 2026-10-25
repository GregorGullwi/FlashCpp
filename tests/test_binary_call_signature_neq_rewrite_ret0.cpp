namespace compare {
	struct Value {
		int data;
		friend bool operator==(const Value& left, const Value& right) { return left.data == right.data; }
	};

	struct Token { int data; };
	bool operator==(const Token& left, const Token& right) { return left.data == right.data; }
}

int main() {
	compare::Value first{3};
	compare::Value same{3};
	compare::Value other{8};
	compare::Token token{4};
	compare::Token matching{4};
	compare::Token different{9};
	static_assert(sizeof(decltype(first != same)) == sizeof(bool));
	static_assert(sizeof(decltype(token != matching)) == sizeof(bool));
	if (first != same) return 1;
	if (!(first != other)) return 2;
	if (token != matching) return 3;
	if (!(token != different)) return 4;
	return 0;
}
