namespace Source {
	using Ptr = int*;
	using Ref = int&;
}

namespace Imported {
	using Source::Ptr;
	using Source::Ref;

	int run() {
		int value = 21;
		Ptr ptr = &value;
		Ref ref = value;

		ref += 21;
		if (*ptr != 42) {
			return 1;
		}
		return 0;
	}
}

int main() {
	return Imported::run();
}
