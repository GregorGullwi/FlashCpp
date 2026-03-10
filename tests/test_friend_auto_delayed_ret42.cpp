struct Box {
	int value;
	friend auto getValue(Box box) { return box.value; }
};

int main() {
	Box box{42};
	return getValue(box);
}