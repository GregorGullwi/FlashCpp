template<bool Flag> struct Box {
	explicit(Flag) Box(int input) : value(input) {}
	int value;
};
int main() {
	Box<true> object = 7;
	return 0;
}
