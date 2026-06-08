struct Value {};

Value* operator->(Value& value) {
	return &value;
}

int main() {
	return 0;
}
