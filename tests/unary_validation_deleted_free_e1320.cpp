struct Value {};
int operator+(Value) = delete;

int main() {
	Value value;
	return +value;
}
