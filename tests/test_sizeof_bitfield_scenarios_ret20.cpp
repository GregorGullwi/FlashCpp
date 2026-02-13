struct Bitfields {
	int a : 5;
	int b : 3;
	int : 24;
	int c : 8;
	int d : 16;
};

typedef struct {
	int a : 3;
	char b;
} TypedefBitfield;

struct Outer {
	struct Inner {
		int a : 3;
		char b;
	} in;
};

template<typename T>
struct TemplateBitfield {
	int a : 3;
	T b;
};

int main() {
	return sizeof(Bitfields) + sizeof(TypedefBitfield) + sizeof(Outer) + sizeof(TemplateBitfield<char>);
}
