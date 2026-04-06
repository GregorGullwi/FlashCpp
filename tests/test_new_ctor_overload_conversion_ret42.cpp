struct Box {
	int value;

	Box(long long v) : value(static_cast<int>(v)) {}
};

int main() {
	Box* box = new Box(42);
	return box->value;
}
