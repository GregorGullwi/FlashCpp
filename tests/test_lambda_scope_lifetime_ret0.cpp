// Lambda call operators own an IR scope just like regular functions, so locals
// with destructors run at the end of the body and remain registered for
// exception cleanup. A named local returned through NRVO is destroyed by that
// cleanup when its construction completes but the call exits exceptionally.

extern "C" int printf(const char*, ...);

static int scoped_destructors = 0;
static int nrvo_destructors = 0;
static int bad_destructors = 0;
static int copy_count = 0;

struct Tracked {
	long long magic;

	Tracked(int initial) : magic(initial) {}

	Tracked(const Tracked& other) : magic(other.magic) {
		++copy_count;
	}

	~Tracked() {
		if (magic == 0x1111) {
			++scoped_destructors;
		} else if (magic == 0x2222) {
			++nrvo_destructors;
		} else {
			++bad_destructors;
		}
	}
};

int main() {
	auto scoped = []() {
		Tracked local(0x1111);
	};
	scoped();
	if (scoped_destructors != 1 || bad_destructors != 0) {
		printf("scoped=%d bad=%d\n", scoped_destructors, bad_destructors);
		return 1;
	}

	auto make = []() {
		Tracked local(0x2222);
		throw 5;
		return local;
	};
	try {
		make();
	} catch (int) {
	}
	if (nrvo_destructors != 1 || copy_count != 0 || bad_destructors != 0) {
		printf("nrvo=%d bad=%d copies=%d\n", nrvo_destructors, bad_destructors, copy_count);
		return 2;
	}
	return 0;
}
