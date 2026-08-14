// Qualification conversion ranking must prefer adding fewer cv-qualifiers.
// C++20 [over.ics.rank]/3.2.1: converting T* to volatile T* is a proper
// subsequence of converting T* to const volatile T*, so the volatile overload
// wins. Reduced from MSVC <atomic> __iso_volatile_store32 overloads.

int store32(volatile int*, int) {
	return 0;
}

int store32(const volatile int*, int) {
	return 1;
}

long long store64(volatile long long*, long long) {
	return 0;
}

long long store64(const volatile long long*, long long) {
	return 2;
}

struct Box {
	int value;
	long long extra;
};

int store_box(volatile Box*, int) {
	return 0;
}

int store_box(const volatile Box*, int) {
	return 3;
}

int store32_from_volatile(volatile int* ptr, int value) {
	return store32(ptr, value);
}

int main() {
	int x = 0;
	long long y = 0;
	Box box{0, 0};
	volatile int vx = 0;
	int from_plain = store32(&x, 0);
	long long from_wide = store64(&y, 0);
	int from_struct = store_box(&box, 0);
	int from_volatile = store32_from_volatile(&vx, 0);
	return (from_plain == 0 && from_wide == 0 && from_struct == 0 && from_volatile == 0)
		? 0
		: 1;
}
