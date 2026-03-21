// Deleted copy assignment should be diagnosed for same-type assignment.

struct NoAssign {
	NoAssign() = default;
	NoAssign& operator=(const NoAssign&) = delete;
};

int main() {
	NoAssign source;
	NoAssign dest;
	dest = source;
	return 0;
}
