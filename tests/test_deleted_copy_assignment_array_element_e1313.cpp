// Deleted copy assignment should be diagnosed through array element assignment.

// Diagnostic regression for deleted copy assignment.
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
