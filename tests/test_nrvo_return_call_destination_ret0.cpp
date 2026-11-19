// "return make();" is a prvalue return: make() must construct its named local
// directly in the caller destination instead of a temporary that is copied
// into the hidden return slot of the forwarding function.

extern "C" int printf(const char*, ...);

static int constructor_count = 0;
static int destructor_count = 0;
static int copy_count = 0;
static void* constructed_address = nullptr;

struct Big {
	long long value;
	long long extra;

	Big(int initial) : value(initial), extra(initial) {
		++constructor_count;
		constructed_address = this;
	}

	Big(const Big& other) : value(other.value), extra(other.extra) {
		++copy_count;
	}

	~Big() {
		++destructor_count;
	}
};

static Big make(int initial) {
	Big local(initial);
	local.value += 1;
	return local;
}

static Big forward(int initial) {
	return make(initial);
}

int main() {
	{
		Big result = forward(41);
		if (result.value != 42 || result.extra != 41) {
			return 1;
		}
		if (constructed_address != &result) {
			return 2;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			printf("constructors=%d copies=%d destructors=%d\n",
				constructor_count, copy_count, destructor_count);
			return 3;
		}
	}
	if (destructor_count != 1) {
		return 4;
	}
	return 0;
}
