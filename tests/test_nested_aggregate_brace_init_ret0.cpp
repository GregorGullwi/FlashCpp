struct Leaf { long long value; };
struct Layer0 { Leaf inner; };
struct Layer1 { Layer0 inner; };
struct Layer2 { Layer1 inner; };

Layer2 makeBraced(int value) { return {{{{value}}}}; }
Layer2 makeTyped(int value) { return Layer2{{{{value}}}}; }

struct Counted { int value; Counted(int input) : value(input + 1) {} };
struct CountedMid { Counted inner; };
struct CountedOuter { CountedMid inner; };

int main() {
	int value = -37;
	Layer2 local = {{{{value}}}};
	Layer2 braced = makeBraced(value);
	Layer2 typed = makeTyped(value);
	CountedOuter counted{{{value}}};
	const bool nested_ok = local.inner.inner.inner.value == -37 && braced.inner.inner.inner.value == -37 && typed.inner.inner.inner.value == -37;
	const bool constructor_ok = counted.inner.inner.value == -36;
	return nested_ok && constructor_ok ? 0 : 1;
}
