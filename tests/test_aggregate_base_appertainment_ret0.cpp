struct ArrayBase { int values[1]; };
struct ArrayDerived : ArrayBase { int tail; };

struct CopyBase { int x, y; };
struct CopyDerived : CopyBase { int tail; };

struct Point { int x, y; };
struct PointerBase { Point* pointer; };
struct PointerDerived : PointerBase { int tail; };

struct Record { int a, b; };
struct NestedBase { int value; Record record; };
struct NestedDerived : NestedBase { int tail; };

struct Empty {};
struct EmptyDerived : Empty { int tail; };

int main() {
	ArrayDerived array{1, 2};
	if (array.values[0] != 1 || array.tail != 2) return 1;

	CopyBase copy_source{3, 4};
	CopyDerived copy{copy_source, 5};
	if (copy.x != 3 || copy.y != 4 || copy.tail != 5) return 2;

	Point point{6, 7};
	PointerDerived pointer{&point, 8};
	if (pointer.pointer != &point || pointer.tail != 8) return 3;

	NestedDerived nested{9, {10, 11}, 12};
	if (nested.value != 9 || nested.record.a != 10 || nested.record.b != 11 || nested.tail != 12) return 4;

	EmptyDerived empty{Empty{}, 13};
	if (empty.tail != 13) return 5;

	return 0;
}
