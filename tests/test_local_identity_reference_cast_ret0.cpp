int main() {
	int value = 4;
	int& alias = static_cast<int&>(value);
	alias += 3;
	return value == 7 ? 0 : 1;
}
