using Callback = long long (*)(short);
using NarrowCallback = int (*)(int);

int increment(int value) {
	return value + 1;
}

int decrement(int value) {
	return value - 1;
}

int safeIncrement(int value) noexcept {
	return value + 2;
}

long long widen(short value) {
	return 10000000000LL + value;
}

long long reduce(short value) {
	return 20000000000LL - value;
}

long long selectAddress(bool condition) {
	Callback selected = condition ? &widen : &reduce;
	return selected(7);
}

int main() {
	if (selectAddress(true) != 10000000007LL) {
		return 1;
	}
	if (selectAddress(false) != 19999999993LL) {
		return 2;
	}
	NarrowCallback narrow = true ? &increment : &decrement;
	if (narrow(7) != 8) {
		return 3;
	}
	NarrowCallback nullable = true ? &increment : nullptr;
	if (nullable(9) != 10) {
		return 4;
	}
	NarrowCallback empty = false ? &increment : nullptr;
	if (empty != nullptr) {
		return 5;
	}
	NarrowCallback relaxed = true ? &safeIncrement : &increment;
	if (relaxed(7) != 9) {
		return 6;
	}
	NarrowCallback decayed = false ? safeIncrement : increment;
	return decayed(7) == 8 ? 0 : 7;
}
