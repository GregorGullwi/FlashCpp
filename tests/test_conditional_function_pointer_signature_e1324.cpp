using FirstCallback = int (*)(int);
using SecondCallback = int (*)(short);

int first(int value) {
	return value + 1;
}

int second(short value) {
	return value + 2;
}

int main() {
	FirstCallback left = &first;
	SecondCallback right = &second;
	auto selected = true ? left : right;
	return selected(1);
}
