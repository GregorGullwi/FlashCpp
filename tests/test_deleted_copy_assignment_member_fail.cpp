// Deleted copy assignment should be diagnosed through member lvalue assignment.

struct NoAssign {
	NoAssign() = default;
	NoAssign& operator=(const NoAssign&) = delete;
};

struct Wrapper {
	NoAssign member;
};

int main() {
	Wrapper wrapper;
	NoAssign source;
	wrapper.member = source;
	return 0;
}
