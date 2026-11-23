// sizeof of an array of unknown bound is ill-formed ([expr.sizeof]/2): the
// bound is not an object extent, so it must not be sized as one element.

int main() {
	return static_cast<int>(sizeof(int[]));
}
