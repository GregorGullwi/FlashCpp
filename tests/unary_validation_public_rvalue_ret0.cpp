class Value {
	int operator-() & { return 1; }
public:
	int operator-() && { return 42; }
};
int main() { return -Value{} == 42 ? 0 : 1; }
