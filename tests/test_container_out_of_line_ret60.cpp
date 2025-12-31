// Test out-of-line member function definitions for custom containers
// Tests range-based for loop with begin()/end() methods

struct SimpleContainer {
	int data[3];
	int* begin();
	int* end();
};

int* SimpleContainer::begin() {
	return &data[0];
}

int* SimpleContainer::end() {
	return &data[3];
}

int main() {
	SimpleContainer container;
	container.data[0] = 10;
	container.data[1] = 20;
	container.data[2] = 30;
	
	int sum = 0;
	for (int x : container) {
		sum += x;
	}
	
	return sum;  // Expected: 60
}
