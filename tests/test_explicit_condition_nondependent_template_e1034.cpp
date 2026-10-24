int condition = 0;
template<class T> struct Box {
	explicit(condition) Box(int) {}
};
int main() { return 0; }
