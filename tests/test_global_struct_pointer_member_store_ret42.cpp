struct W {
	char prefix;
	short code;
	long tag;
};

W wv;
W* wp;

int main() {
	wp = &wv;
	wp->code = 7;
	wp->tag = 42;
	if (wp->tag != 42 || wv.code != 7) {
		return 1;
	}
	return wv.tag;
}
