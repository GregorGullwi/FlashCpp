struct Container {
	int data[3];
	int* begin();
	int* end();
};

int* Container::begin() {
	return &data[0];
}

int* Container::end() {
	return &data[3];
}

int main() {
	Container c;
	c.data[0] = 1;
	c.data[1] = 2;
	c.data[2] = 3;
	
	int sum = 0;
	for (int x : c) {
		sum += x;
	}
	return sum;  // Should be 6
}
