struct Box {
	int value;
};

Box global_box{42};

Box* getBox() {
	return &global_box;
}

int main() {
	return getBox()->value == 42 ? 0 : 1;
}
