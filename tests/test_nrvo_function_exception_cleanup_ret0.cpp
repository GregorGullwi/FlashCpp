// An NRVO local lives in the caller's hidden return slot, so exception cleanup
// must destroy the object through the address stored in the frame, not the
// frame slot that holds that address.

extern "C" int printf(const char*, ...);

static int good_destructors = 0;
static int bad_destructors = 0;
static int copy_count = 0;

struct Large {
	long long magic;
	long long extra;

	Large(int initial) : magic(initial), extra(1) {}

	Large(const Large& other) : magic(other.magic), extra(other.extra) {
		++copy_count;
	}

	~Large() {
		if (magic == 0x12345678) {
			++good_destructors;
		} else {
			++bad_destructors;
		}
	}
};

Large make_throwing_large() {
	Large local(0x12345678);
	local.extra = 9;
	throw 5;
	return local;
}

int main() {
	try {
		make_throwing_large();
	} catch (int) {
	}
	if (good_destructors != 1 || bad_destructors != 0 || copy_count != 0) {
		printf("good=%d bad=%d copies=%d\n", good_destructors, bad_destructors, copy_count);
		return 1;
	}
	return 0;
}
