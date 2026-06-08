struct Counter {
	int value;

	Counter& operator--() {
		value--;
		return *this;
	}

	Counter operator--(int) {
		Counter temp;
		temp.value = value;
		value--;
		return temp;
	}
};

int main() {
	Counter c1;
	c1.value = 15;

	--c1;
	c1--;
	Counter& ref = --c1;

	return c1.value + 30;
}
