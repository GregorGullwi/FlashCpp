int values[2] = {17, 29};

int* element(short index) {
	return &values[index];
}

struct Receiver {
	int value;
	Receiver(int* (&function)(short), short index) : value(*function(index)) {}
};

int invoke(int* (&&function)(short), short index) {
	return *function(index);
}

int main() {
	Receiver receiver(element, 1);
	return receiver.value == 29 && invoke(element, 0) == 17 ? 0 : 1;
}
