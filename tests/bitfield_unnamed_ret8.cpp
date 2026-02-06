// Test: Named and unnamed bitfield members in structs
struct Bitfields {
	int a : 5;
	int b : 3;
	int : 24;      // unnamed padding bitfield
	int c : 8;
	int d : 16;
};

int main() {
	Bitfields bf;
	bf.a = 1;
	bf.d = 7;
	return bf.a + bf.d;
}
