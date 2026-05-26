struct Choose {
	template<typename T>
	Choose(long, T) {}

	template<typename T>
	Choose(T, int) {}

	template<typename T>
	Choose(int, T) {}
};

int main() {
	short value = 0;
	Choose chosen(value, value);
	return 0;
}
