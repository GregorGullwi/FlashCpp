struct Box {
	int value;

	Box(double v) : value(static_cast<int>(v * 2.0)) {}
};

int main() {
	Box* box = new Box(21);
	return box->value;
}
