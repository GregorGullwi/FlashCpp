typedef struct { short value; } Small;
namespace records {
	typedef union { long long wide; unsigned char byte; } Choice;
}
int main() {
	typedef struct Local { long long value; } Local;
	Small small{13};
	Small copied{};
	copied = small;
	records::Choice original{};
	original.wide = 12345678901LL;
	records::Choice choice{};
	choice = static_cast<records::Choice&&>(original);
	Local first{42};
	Local second{};
	second = first;
	return copied.value == 13 && choice.wide == 12345678901LL && second.value == 42 ? 0 : 1;
}
