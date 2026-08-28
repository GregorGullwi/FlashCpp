// Deleted copy assignment should be diagnosed through indirection assignment.

// Diagnostic regression for deleted copy assignment.
struct NoAssign {
	NoAssign() = default;
	NoAssign& operator=(const NoAssign&) = delete;
};

int main() {
	NoAssign destination;
	NoAssign source;
	NoAssign* ptr = &destination;
	*ptr = source;
	return 0;
}
