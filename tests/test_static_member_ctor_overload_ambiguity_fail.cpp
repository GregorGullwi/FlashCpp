struct Pick {
	Pick(long) {}
	Pick(long long) {}
};

struct Holder {
	static const Pick pick;
};

const Pick Holder::pick = Pick(1);

int main() {
	return 0;
}