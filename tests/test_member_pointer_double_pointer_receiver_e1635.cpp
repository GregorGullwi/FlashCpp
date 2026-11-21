struct S {
	int value;
};

int read(S** receiver, int S::* member) {
	return receiver->*member;
}
