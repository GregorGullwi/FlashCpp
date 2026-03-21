// Deleted move assignment should be diagnosed through indirection assignment.

struct NoMoveAssign {
	NoMoveAssign() = default;
	NoMoveAssign& operator=(NoMoveAssign&&) = delete;
};

int main() {
	NoMoveAssign destination;
	NoMoveAssign source;
	NoMoveAssign* ptr = &destination;
	*ptr = static_cast<NoMoveAssign&&>(source);
	return 0;
}
