enum Flags {
	One = 1
};

int operator&(double lhs, Flags rhs) {
	return static_cast<int>(lhs) & static_cast<int>(rhs);
}

int main() {
	return (2.0 & One) == 0 ? 0 : 1;
}
