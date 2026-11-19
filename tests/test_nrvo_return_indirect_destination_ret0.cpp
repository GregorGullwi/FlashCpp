// Function-pointer data members and member-function-pointer calls follow the
// same result-destination contract as direct and member calls. Both a return
// expression and a local initializer must construct in the receiver storage.

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

static Big make_target(int initial) {
	return Big(initial + 200);
}

struct Holder {
	Big (*function_pointer)(int);
};

struct Taker {
	Big take(int initial) {
		return Big(initial + 100);
	}
};

struct VirtualBase {
	virtual Big take(int initial) {
		return Big(initial + 300);
	}
};

struct VirtualTaker : VirtualBase {
	Big take(int initial) override {
		return Big(initial + 100);
	}
};

static Big call_member(Taker& taker, int initial) {
	return taker.take(initial);
}

static Big call_virtual(VirtualBase& base, int initial) {
	return base.take(initial);
}

static Big call_function_pointer(Holder holder, int initial) {
	return holder.function_pointer(initial);
}

static Big call_member_pointer(Taker& taker, Big (Taker::*member)(int), int initial) {
	return (taker.*member)(initial);
}

static void resetCounters() {
	constructor_count = 0;
	destructor_count = 0;
	copy_count = 0;
	constructed_address = nullptr;
}

int main() {
	resetCounters();
	{
		Holder holder{make_target};
		Big result = call_function_pointer(holder, 7);
		if (result.value != 207) {
			return 1;
		}
		if (constructed_address != &result) {
			return 2;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			printf("function pointer constructors=%d copies=%d destructors=%d\n",
				constructor_count, copy_count, destructor_count);
			return 3;
		}
	}
	if (destructor_count != 1) {
		return 4;
	}

	resetCounters();
	{
		Taker taker;
		Big result = call_member(taker, 5);
		if (result.value != 105) {
			return 9;
		}
		if (constructed_address != &result) {
			return 10;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			printf("member return constructors=%d copies=%d destructors=%d\n",
				constructor_count, copy_count, destructor_count);
			return 11;
		}
	}
	if (destructor_count != 1) {
		return 12;
	}

	resetCounters();
	{
		VirtualTaker taker;
		VirtualBase& base = taker;
		Big result = call_virtual(base, 7);
		if (result.value != 107) {
			return 13;
		}
		if (constructed_address != &result) {
			return 14;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			printf("virtual return constructors=%d copies=%d destructors=%d\n",
				constructor_count, copy_count, destructor_count);
			return 15;
		}
	}
	if (destructor_count != 1) {
		return 16;
	}

	resetCounters();
	Taker taker;
	{
		Big result = call_member_pointer(taker, &Taker::take, 9);
		if (result.value != 109) {
			return 17;
		}
		if (constructed_address != &result) {
			return 18;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			printf("member pointer return constructors=%d copies=%d destructors=%d\n",
				constructor_count, copy_count, destructor_count);
			return 19;
		}
	}
	if (destructor_count != 1) {
		return 20;
	}

	resetCounters();
	Taker initializer_taker;
	Big (Taker::*member)(int) = &Taker::take;
	{
		Big result = (initializer_taker.*member)(11);
		if (result.value != 111) {
			return 21;
		}
		if (constructed_address != &result) {
			return 22;
		}
		if (constructor_count != 1 || copy_count != 0 || destructor_count != 0) {
			printf("member pointer initializer constructors=%d copies=%d destructors=%d\n",
				constructor_count, copy_count, destructor_count);
			return 23;
		}
	}
	if (destructor_count != 1) {
		return 24;
	}
	return 0;
}
