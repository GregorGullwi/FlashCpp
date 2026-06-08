struct Counter {
	Counter& operator--(int first, int second) {
		return *this;
	}
};

int main() {
	return 0;
}
