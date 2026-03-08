int value = 10;

int getGlobalValue() {
	return value;
}

int main() {
	int value = 5;
	value += 4;
	return value * 10 + getGlobalValue();
}
