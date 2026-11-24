// [dcl.array]/1: only the outermost array bound may be omitted. A template
// type argument with a misplaced inner omitted bound is diagnosed where the
// bound is written, not at a later sizeof of the collapsed int[] shape.
template <typename T>
struct Probe {
	int value;
};

Probe<int[3][]> object;

int main() {
	return object.value;
}
