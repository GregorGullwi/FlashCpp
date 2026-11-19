// In "return Big(Tag(7))" only the expression that constructs the result may
// use the hidden return slot. The nested Tag temporary must have its own
// storage and be destroyed at the end of the full expression.

extern "C" int printf(const char*, ...);

static int big_constructor_count = 0;
static int big_destructor_count = 0;
static int big_copy_count = 0;
static int tag_constructor_count = 0;
static int tag_destructor_count = 0;
static void* big_address = nullptr;
static void* tag_address = nullptr;

struct Tag {
	long long value;

	Tag(int initial) : value(initial) {
		++tag_constructor_count;
		tag_address = this;
	}

	Tag(const Tag& other) : value(other.value) {
		++tag_constructor_count;
		tag_address = this;
	}

	~Tag() {
		++tag_destructor_count;
	}
};

struct Big {
	long long value;
	long long extra;

	Big(const Tag& tag) : value(tag.value), extra(tag.value + 1) {
		++big_constructor_count;
		big_address = this;
	}

	Big(const Big& other) : value(other.value), extra(other.extra) {
		++big_copy_count;
	}

	~Big() {
		++big_destructor_count;
	}
};

static Big build(int initial) {
	return Big(Tag(initial));
}

int main() {
	{
		Big result = build(7);
		if (result.value != 7 || result.extra != 8) {
			return 1;
		}
		if (big_address != &result) {
			return 2;
		}
		if (tag_address == nullptr || tag_address == big_address) {
			printf("tag=%p big=%p\n", tag_address, big_address);
			return 3;
		}
		// The Tag temporary dies at the end of the return expression.
		if (tag_destructor_count != 1) {
			printf("tag destructors=%d\n", tag_destructor_count);
			return 4;
		}
		if (big_constructor_count != 1 || big_copy_count != 0 || big_destructor_count != 0) {
			printf("big constructors=%d copies=%d destructors=%d\n",
				big_constructor_count, big_copy_count, big_destructor_count);
			return 5;
		}
	}
	if (big_destructor_count != 1 || tag_destructor_count != 1) {
		return 6;
	}
	return 0;
}
