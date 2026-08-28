// Deleted move assignment should be diagnosed for xvalue assignment.

// Diagnostic regression for deleted move assignment.
struct NoMoveAssign {
	NoMoveAssign() = default;
	NoMoveAssign& operator=(NoMoveAssign&&) = delete;
};

int main() {
	NoMoveAssign source;
	NoMoveAssign dest;
	dest = static_cast<NoMoveAssign&&>(source);
	return 0;
}
