class Value {
	int operator-() && { return 42; }
public:
	int operator-() & { return 1; }
};
int main() { return -Value{}; }
