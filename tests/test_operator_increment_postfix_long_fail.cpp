struct Counter {
	Counter operator++(long) {
		return *this;
	}
};

int main() {
	return 0;
}
