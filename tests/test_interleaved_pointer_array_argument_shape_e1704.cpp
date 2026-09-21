// A non-projectable interleaved pointer/array argument whose ordered spine
// differs from the parameter must be rejected as an ordinary no-match instead
// of reaching the flat projection guard and aborting compilation.
int (*(*value)[3])[4] = nullptr;

int other(int (*(*)[5])[6]);

int main() {
	other(value);
	return 0;
}
