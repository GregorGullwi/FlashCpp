// C++20 [conv.array]: int[3] decays to int*, so it cannot initialize int**.
// Array-to-pointer decay is now planned from canonical types, and that plan's
// no-match is authoritative instead of being re-tried against the flat
// compatibility shape.
int values[3];

int consume(int** pointer);

int main() {
	return consume(values);
}
