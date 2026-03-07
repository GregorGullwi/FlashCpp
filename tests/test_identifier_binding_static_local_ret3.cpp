int nextCounter() {
	static int counter = 0;
	counter += 1;
	return counter;
}

int main() {
	nextCounter();
	nextCounter();
	return nextCounter();
}
