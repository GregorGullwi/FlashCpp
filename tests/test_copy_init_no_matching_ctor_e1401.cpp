enum class Color { Red };

struct OnlyDouble {
	OnlyDouble(double) {}
};

int main() {
	OnlyDouble value = Color::Red;
	return 0;
}
