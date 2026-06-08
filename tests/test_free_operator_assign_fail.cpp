struct Value {};

Value& operator=(Value& lhs, const Value& rhs) {
	return lhs;
}

int main() {
	return 0;
}
