// Passing a non-projectable array member directly to a reference parameter
// must materialize the member address from the ordered declarator. The flat
// member fields cannot describe int (*cells[N])[M], so the call argument used
// to receive an unemitted temporary instead of the member's address.
int storage[4] = {7, 8, 9, 10};
int other[4] = {1, 2, 3, 4};

struct Holder {
	int (*cells[2])[4];
};

int sum_first_and_last(int (* (&rows)[2])[4]) {
	return (*rows[0])[0] + (*rows[1])[3];
}

int main() {
	Holder holder{{&storage, &other}};
	if (sum_first_and_last(holder.cells) != 11) return 1;

	int (* (&rows)[2])[4] = holder.cells;
	if ((*rows[1])[2] != 3) return 2;
	return 42;
}
