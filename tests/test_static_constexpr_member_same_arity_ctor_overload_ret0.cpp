struct Pick {
	int which;

	Pick(int)
		: which(1) {}

	Pick(double)
		: which(2) {}
};

struct Holder {
	static const Pick pick;
};

const Pick Holder::pick = Pick(1.5);

int main() {
	Pick copy = Holder::pick;
	return copy.which == 2 ? 0 : 1;
}