namespace Source {
	using Ptr = int*;
	using Ref = int&;
	using Arr = int[3];

	int twice(int x) {
		return x * 2;
	}

	using Fn = int (*)(int);
}

namespace Imported {
	using Source::Ptr;
	using Source::Ref;
	using Source::Arr;
	using Source::Fn;

	int run() {
		int value = 21;
		Ptr ptr = &value;
		Ref ref = value;
		Arr arr = {1, 2, 3};
		Fn fn = Source::twice;

		ref += arr[0];
		if (*ptr != 22) {
			return 1;
		}
		if (arr[2] != 3) {
			return 2;
		}
		if (fn(*ptr) != 44) {
			return 3;
		}
		return 0;
	}
}

int main() {
	return Imported::run();
}
