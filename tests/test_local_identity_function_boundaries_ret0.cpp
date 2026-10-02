// Local IDs and debug names restart independently in each function.
int firstValue() {
	int first = 3;
	{
		short first = 7;
		if (first != 7) return 0;
	}
	return first;
}

int secondValue() {
	char second = 6;
	return second;
}

int main() {
	int combined = firstValue() + secondValue();
	return combined == 9 ? 0 : 1;
}
