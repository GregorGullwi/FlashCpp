int condition = 0;
struct Box {
	explicit(condition) Box(int) {}
};
int main() { return 0; }
