// A private static member-function template has to carry its owning class
// identity so access policy sees both the owner and the friend grant.
// Without the owner the compiler cannot resolve the access and must not fall
// back to accepting the call.

class Vault {
	template <typename T>
	static int compute() {
		return static_cast<int>(sizeof(T)) + 41;
	}

	friend struct Opener;
};

struct Opener {
	template <typename T>
	static int open() {
		return Vault::template compute<T>();
	}
};

int main() {
	return Opener::open<int>() - 45;
}
