enum Flags {
	One = 1,
	Two = 2
};

int main() {
	int bits = One | Two;
	int shifted_left = bits << 1;
	int shifted_right = shifted_left >> 1;
	int masked = shifted_right & Two;
	int toggled = masked ^ One;
	return bits == 3 && shifted_left == 6 && shifted_right == 3 && masked == 2 && toggled == 3 ? 0 : 1;
}
