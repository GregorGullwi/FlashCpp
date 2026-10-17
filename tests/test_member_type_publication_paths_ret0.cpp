int choose(const char*) { return 1; }
int choose(char*) { return 2; }
int choose(const double*) { return 4; }
int choose(double*) { return 8; }

struct Payload { int value; };
int choose(const Payload*) { return 16; }
int choose(Payload*) { return 32; }

typedef struct {
	const double* data;
} FloatingView;

struct Variant {
	union {
		const char* text;
		const Payload* object;
	};
};

template <typename T>
struct Holder { T data; };

int answer() { return 42; }

struct Callable {
	int (*function)();
};

int main() {
	char text = 'a';
	double number = 3.5;
	Payload payload{7};
	FloatingView floating{&number};
	Variant variant;
	variant.text = &text;
	int selected = choose(variant.text) + choose(floating.data);
	variant.object = &payload;
	selected += choose(variant.object);
	Holder<const Payload*> holder{&payload};
	selected += choose(holder.data);
	const char* pointer = &text;
	Holder<const char*&> reference{pointer};
	selected += choose(reference.data);
	auto captured = [pointer]() { return choose(pointer); };
	Callable callable{answer};
	return selected == 38 && captured() == 1 && callable.function() == 42 ? 0 : 1;
}
