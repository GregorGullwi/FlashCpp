// C++20 [conv.array]: an array decays to a pointer to its first element, so
// long long[4] becomes long long*, never a pointer to the whole array
// (long long (*)[4]). The canonical structural plan decides this shape, and
// its no-match is authoritative for a supported array/pointer pair.
long long values[4];

int consume(long long (*row)[4]);

int main() {
	return consume(values);
}
