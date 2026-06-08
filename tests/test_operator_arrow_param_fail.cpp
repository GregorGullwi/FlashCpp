struct ArrowWrapper {
	ArrowWrapper* operator->(int extra) {
		return this;
	}
};

int main() {
	return 0;
}
