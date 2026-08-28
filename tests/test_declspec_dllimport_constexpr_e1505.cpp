// Expected error: constexpr and __declspec(dllimport) are incompatible
__declspec(dllimport) constexpr int x = 42;

int main() {
	return x;
}
