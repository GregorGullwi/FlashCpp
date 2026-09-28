enum NarrowFixed : unsigned char { narrow_value = 1 };
enum WideFixed : unsigned long long { wide_value = 1 };
enum Ordinary { ordinary_value = 1 };

struct NarrowChoice {
	char bytes[1];
};

struct WideChoice {
	char bytes[2];
};

struct IntChoice {
	char bytes[3];
};

enum ForwardFixed : unsigned char;

NarrowChoice select(unsigned char);
WideChoice select(unsigned long long);
IntChoice select(int);
NarrowChoice select_forward(unsigned char);
IntChoice select_forward(int);
ForwardFixed forward_value();

static_assert(sizeof(decltype(select(NarrowFixed::narrow_value))) == sizeof(NarrowChoice));
static_assert(sizeof(decltype(select(WideFixed::wide_value))) == sizeof(WideChoice));
static_assert(sizeof(decltype(select(Ordinary::ordinary_value))) == sizeof(IntChoice));
static_assert(sizeof(decltype(select_forward(forward_value()))) == sizeof(NarrowChoice));

enum ForwardFixed : unsigned char { forward_value_enum = 1 };

int main() {
	return 0;
}
