// A constexpr function body resolves unqualified names in the namespace where
// it is declared, not in the namespace that triggered the call.

constexpr int k = 7;
constexpr int read_k() {
	return k;
}

namespace N {
constexpr int k = 111;
constexpr int via_call = read_k();
}

static_assert(N::via_call == 7, "function body must use its declaration namespace");

int main() {
	return N::via_call;
}
