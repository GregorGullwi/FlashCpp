struct Holder {
	int value;

	int& getRef() {
		return value;
	}
};

int main() {
	Holder h;
	h.value = 0;

	h.getRef() = 40;
	h.getRef() += 2;

	return h.value;
}
