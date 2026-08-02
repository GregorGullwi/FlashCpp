// Pointer conversion must reject an ambiguous base subobject as well.

struct Base {
	int value;
};

struct Left : Base {};
struct Right : Base {};
struct Diamond : Left, Right {};

int main() {
	Diamond value;
	Base* base = &value;
	return base->value;
}
