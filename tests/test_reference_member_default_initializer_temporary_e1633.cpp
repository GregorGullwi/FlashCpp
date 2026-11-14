struct Temporary {
	Temporary() {}
};

struct Holder {
	const Temporary& reference = Temporary{};
};

int main() {
	Holder holder;
	(void)holder;
	return 0;
}
