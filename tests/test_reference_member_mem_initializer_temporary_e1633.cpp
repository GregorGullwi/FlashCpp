struct Temporary {
	Temporary() {}
};

struct Holder {
	Holder() : reference(Temporary{}) {}
	const Temporary& reference;
};

int main() {
	Holder holder;
	(void)holder;
	return 0;
}
