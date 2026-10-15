template <class T>
struct Box {
	T value;
};

int main() {
	Box<int> boxes[3] = {{1}, {2}, {3}};
	Box<int>* second = &boxes[1];
	if (second->value != 2) return 1;
	boxes[2].value = 9;
	if (boxes[2].value != 9) return 2;
	Box<long> wide[2] = {{4}, {5}};
	return wide[1].value == 5 ? 0 : 3;
}
