struct Pick {
	int which;

	Pick(int)
		: which(1) {}

	Pick(double)
		: which(2) {}
};

Pick makeParenDouble() {
	return Pick(1.5);
}

Pick makeBraceDouble() {
	return Pick{1.5};
}

Pick makeParenInt() {
	return Pick(1);
}

Pick makeBraceInt() {
	return Pick{1};
}

int main() {
	return (makeParenDouble().which == 2 &&
			makeBraceDouble().which == 2 &&
			makeParenInt().which == 1 &&
			makeBraceInt().which == 1)
			   ? 0
			   : 1;
}
