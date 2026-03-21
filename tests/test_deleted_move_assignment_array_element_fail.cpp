// Deleted move assignment should be diagnosed through array element assignment.

struct NoMoveAssign {
	NoMoveAssign() = default;
	NoMoveAssign& operator=(NoMoveAssign&&) = delete;
};

int main() {
	NoMoveAssign values[1];
	NoMoveAssign source;
	values[0] = static_cast<NoMoveAssign&&>(source);
	return 0;
}
