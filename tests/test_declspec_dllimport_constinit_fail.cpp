// Expected error: constinit and __declspec(dllimport) are incompatible
__declspec(dllimport) constinit int x = 42;

int main() {
	return x;
}
