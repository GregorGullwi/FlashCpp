// Deleted copy assignment should be diagnosed through array element assignment.

struct NoAssign {
	NoAssign() = default;
	NoAssign& operator=(const NoAssign&) = delete;
};

int main() {
	NoAssign values[1];
	NoAssign source;
	values[0] = source;
	return 0;
}
