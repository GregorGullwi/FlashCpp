// Diagnostic regression for increment/decrement operator forms.
struct Counter {
	Counter operator++(long) {
		return *this;
	}
};

int main() {
	return 0;
}
