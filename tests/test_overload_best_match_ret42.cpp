// Test that overload resolution picks the correct overload by argument types,
// not the first registered one (regression for SymbolTable.h fallback bug).

int pick(int x) { return x; }
int pick(int x, int y) { return x + y; }
int pick(int x, int y, int z) { return x + y + z; }

int main() {
	// Must call pick(int,int) — returns 40+2 = 42
	return pick(40, 2);
}
