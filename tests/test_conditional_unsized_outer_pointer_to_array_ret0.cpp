struct Cell {
	int value;
};

template <class T, int Columns>
using IncompleteRows = T(*)[][Columns];

int main() {
	Cell a[2][2] = {{{1}, {2}}, {{3}, {4}}};
	Cell b[3][2] = {{{5}, {6}}, {{7}, {8}}, {{9}, {10}}};
	IncompleteRows<Cell, 2> p = &a;
	IncompleteRows<Cell, 2> q = &b;
	IncompleteRows<Cell, 2> from_true = true ? p : q;
	if (from_true != p) return 1;
	IncompleteRows<Cell, 2> from_false = false ? p : q;
	if (from_false != q) return 2;
	if ((*from_true)[1][1].value != 4) return 3;

	int plain[3] = {7, 8, 9};
	int other[4] = {1, 2, 3, 4};
	int* decayed = true ? plain : other;
	if (decayed[0] != 7 || decayed[2] != 9) return 4;

	return 0;
}
