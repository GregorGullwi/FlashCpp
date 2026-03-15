// Regression test: range-for with auto loop variable on a container whose
// begin()/end() return a struct iterator.  The loop variable should be deduced
// from the iterator's operator*() return type (int), NOT from the iterator
// struct type itself.
//
// Bug: resolveRangedForLoopDecl receives begin_return_type (IntIter) and,
// because pointer_depth()==0, passes the iterator type unchanged as the
// deduced element type — so `auto x` becomes `IntIter x` instead of `int x`.

struct IntIter {
	int* ptr;

	int& operator*() { return *ptr; }

	IntIter& operator++() {
		++ptr;
		return *this;
	}

	bool operator!=(const IntIter& other) const {
		return ptr != other.ptr;
	}
};

struct Container {
	int data[3];

	IntIter begin() {
		IntIter it;
		it.ptr = &data[0];
		return it;
	}

	IntIter end() {
		IntIter it;
		it.ptr = &data[3];
		return it;
	}
};

int main() {
	Container c;
	c.data[0] = 10;
	c.data[1] = 20;
	c.data[2] = 30;

	int sum = 0;
	for (auto x : c) {
		sum = sum + x;
	}

	// Expected: 60 (10+20+30).  With the bug, x is deduced as IntIter
	// instead of int, producing incorrect codegen.
	return sum == 60 ? 0 : 1;
}
