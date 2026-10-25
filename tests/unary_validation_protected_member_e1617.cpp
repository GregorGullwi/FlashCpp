struct Base {
protected:
	int operator-() { return 1; }
};
struct Value : Base {};

int main() {
	Value value;
	return -value;
}
