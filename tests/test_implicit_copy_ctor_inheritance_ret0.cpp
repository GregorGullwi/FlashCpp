struct Left {
	int left;
	Left(int v = 0) : left(v) {}
};

struct Right {
	int right;
	Right(int v = 0) : right(v) {}
};

struct MultiDerived : Left, Right {
	int value;
	MultiDerived(int l, int r, int v) : Left(l), Right(r), value(v) {}
};

struct VBase {
	int vb;
	VBase(int v = 0) : vb(v) {}
};

struct VirtualDerived : virtual VBase {
	int value;
	VirtualDerived(int b, int v) : VBase(b), value(v) {}
};

int main() {
	MultiDerived multi(11, 22, 33);
	MultiDerived multi_copy(multi);
	if (multi_copy.left != 11) return 1;
	if (multi_copy.right != 22) return 2;
	if (multi_copy.value != 33) return 3;

	VirtualDerived virt(17, 9);
	VirtualDerived virt_copy(virt);
	if (virt_copy.vb != 17) return 4;
	if (virt_copy.value != 9) return 5;
	return 0;
}