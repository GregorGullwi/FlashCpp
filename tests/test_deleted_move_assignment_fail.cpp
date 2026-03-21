// Deleted move assignment should be diagnosed for xvalue assignment.

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
