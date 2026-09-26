using InnerFunction = int(double);
using OuterFunction = InnerFunction*(int);

struct Holder {
	static OuterFunction* (*value)[3];
};

int main() {
	return sizeof(Holder::value) == sizeof(void*) ? 42 : 0;
}
