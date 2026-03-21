// Deleted copy assignment should be diagnosed through indirection assignment.

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
