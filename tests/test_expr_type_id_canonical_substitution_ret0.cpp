// Expression type-ids in templates must substitute through canonical
// TypeSpecifierNode identity and metadata (pointer, reference, array, cv).
// Return value is 0 only when those substituted type-ids keep the C++20 sizes
// rather than relying on a parser token-name fallback.

template <typename T>
constexpr int pointer_size() {
	return sizeof(T*);
}

template <typename T>
constexpr int array_size() {
	return sizeof(T[2]);
}

template <typename T>
constexpr int reference_size() {
	return sizeof(T&);
}

template <typename T>
constexpr int cv_size() {
	return sizeof(const T);
}

template <typename T>
T identity_cast(T value) {
	return static_cast<T>(value);
}

template <typename T>
using Ptr = T*;

template <typename T>
constexpr int alias_pointer_size() {
	return sizeof(Ptr<T>);
}

struct Pair {
	char a;
	char b;
};

template <typename T>
struct Sizer {
	static constexpr int ptr() {
		return sizeof(T*);
	}

	static constexpr int array_two() {
		return sizeof(T[2]);
	}
};

int main() {
	int result = 0;
	if (pointer_size<char>() != sizeof(char*)) result |= 1;
	if (pointer_size<int>() != sizeof(int*)) result |= 2;
	if (pointer_size<Pair>() != sizeof(Pair*)) result |= 4;
	if (array_size<char>() != 2) result |= 8;
	if (array_size<int>() != 8) result |= 16;
	if (reference_size<char>() != sizeof(char)) result |= 32;
	if (reference_size<int>() != sizeof(int)) result |= 64;
	if (reference_size<long long>() != sizeof(long long)) result |= 128;
	if (cv_size<char>() != sizeof(char)) result |= 256;
	if (cv_size<int>() != sizeof(int)) result |= 512;
	if (alias_pointer_size<int>() != sizeof(int*)) result |= 1024;
	if (alias_pointer_size<Pair>() != sizeof(Pair*)) result |= 2048;
	if (identity_cast<int>(40) != 40) result |= 4096;
	if (identity_cast<char>(2) != 2) result |= 8192;
	if (Sizer<char>::ptr() != sizeof(char*)) result |= 16384;
	if (Sizer<Pair>::ptr() != sizeof(Pair*)) result |= 32768;
	if (Sizer<int>::array_two() != 8) result |= 65536;
	if (Sizer<long long>::array_two() != 16) result |= 131072;
	return result;
}
