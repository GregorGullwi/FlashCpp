using ConstInt = const int;

struct ConstAliasMember {
	ConstInt value;
};

int main() {
	ConstAliasMember object;
	return object.value;
}
