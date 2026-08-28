struct Value {};

bool operator!(const Value&, int extra) {
	return extra != 0;
}

int main() {
	return 0;
}
