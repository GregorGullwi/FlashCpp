struct Box { short value; };
struct Callable { short operator()(int value) const { return value; } };
using Reference = Box&;
using Chained = Reference;
Box object{42};
Chained get() { return object; }
int main() {
	Chained (*pointer)() = &get;
	Callable callable;
	Callable& receiver = callable;
	int value = 42;
	return pointer().value == 42 && sizeof(pointer) == sizeof(void*) && sizeof(receiver(*&value)) == sizeof(short) ? 0 : 1;
}
