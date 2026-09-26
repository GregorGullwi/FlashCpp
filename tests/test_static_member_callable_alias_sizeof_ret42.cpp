using InnerFunction = int(double);
using OuterFunction = InnerFunction*(int);

struct Holder {
	static inline OuterFunction* (*value)[3] = nullptr;
};

int main() {
	return sizeof(Holder::value) == sizeof(void*) &&
		sizeof(*Holder::value) == 3 * sizeof(void*) &&
		sizeof(Holder::value[0]) == 3 * sizeof(void*) &&
		sizeof((*Holder::value)[0]) == sizeof(void*) &&
		sizeof((*Holder::value)[0](1)(2.0)) == sizeof(int)
		? 42
		: 0;
}
