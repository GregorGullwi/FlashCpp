// Expected error: definition of dllimport data is not allowed
__declspec(dllimport) int x = 42;

int main() {
	return x;
}
