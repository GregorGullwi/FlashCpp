// Deleted move assignment should be diagnosed through member lvalue assignment.

// Diagnostic regression for deleted move assignment.
struct NoMoveAssign {
	NoMoveAssign() = default;
	NoMoveAssign& operator=(NoMoveAssign&&) = delete;
};

struct Wrapper {
	NoMoveAssign member;
};

int main() {
	Wrapper wrapper;
	NoMoveAssign source;
	wrapper.member = static_cast<NoMoveAssign&&>(source);
	return 0;
}
