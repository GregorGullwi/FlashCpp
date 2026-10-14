namespace Scoped {
struct Record { long value; };
}

struct Record { double value; };

namespace Other {
struct Record { short first; short second; };
}

int main() {
	Scoped::Record scoped{7};
	Record global{9.0};
	Other::Record other{11, 13};
	return scoped.value == 7 && global.value == 9 && other.first == 11 && other.second == 13
		&& sizeof(Scoped::Record) == sizeof(long) && sizeof(Record) == sizeof(double) ? 0 : 1;
}
