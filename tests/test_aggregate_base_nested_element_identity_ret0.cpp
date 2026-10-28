struct B { int x; };
struct M : B { int y; };
struct R { M m; int z; };

struct MemberBase { int x; };
struct MemberHolder { MemberBase b; int y; };

int main() {
	R nested{1, 2, 3};
	if (nested.m.x != 1 || nested.m.y != 2 || nested.z != 3) return 1;

	MemberBase source{7};
	MemberHolder whole{source, 9};
	if (whole.b.x != 7 || whole.y != 9) return 2;

	return 0;
}
