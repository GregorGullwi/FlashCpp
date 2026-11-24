// [expr.sizeof]/2: long long[][3] is an array of unknown bound, so the direct
// type-id operand is invalid. This pins the shape a materialized template
// argument has to match.
int size_of_array() {
	return static_cast<int>(sizeof(long long[][3]));
}

int main() {
	return size_of_array();
}
