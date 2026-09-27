// A free function that is not a friend must not reach a private static
// member-function template. The qualified call resolves the owning class
// identity so access policy can reject it instead of treating the callee as
// ownerless.

struct Vault {
private:
	template <typename T>
	static int secret() {
		return static_cast<int>(sizeof(T));
	}
};

int read() {
	return Vault::template secret<int>();
}

int main() {
	return read();
}
