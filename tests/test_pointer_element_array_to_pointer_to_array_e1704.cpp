// C++20 [conv.array]: an array of pointers decays to a pointer to its first
// element (int**), never to a pointer to the whole row (int (*)[3]). The
// canonical array-decay plan must stay authoritative here instead of accepting
// the pointer-to-array shape through the compatibility planner.
int* rows[3];

int consume(int (*row)[3]);

int main() {
	return consume(rows);
}
