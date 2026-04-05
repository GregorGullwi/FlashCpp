struct Box {
	int value;

	template <typename T>
	void set(T v) {
		value = static_cast<int>(v);
	}
};

Box& getBox(Box& box) {
	return box;
}

int main() {
	Box box{0};
	getBox(box).set<double>(4.5);
	return box.value == 4 ? 0 : 1;
}
