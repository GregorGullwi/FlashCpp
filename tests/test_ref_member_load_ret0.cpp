// Regression test for reference member access in Load context.
// Bug: after MemberAccess loads the stored pointer for a reference member,
// the IR was missing a Dereference instruction in Load context, causing the
// raw pointer bits to be used as the value instead of the dereferenced value.
//
// Also covers: applyConstructorArgConversion now applies pre-bind type conversions
// for reference parameters (e.g. int → const double&, int → const float&).
//
// Type coverage: int (various sizes), double, float, enum, struct (const read).

// --- int& members ---
struct BoxInt {
	const int& val;
	BoxInt(const int& v) : val(v) {}
	int get() const { return val; }
};

// non-const int& member: mutation must propagate back to the caller
struct Wrapper {
	int& r;
	Wrapper(int& x) : r(x) {}
	void inc() { r = r + 1; }
	void add(int n) { r = r + n; }
	int get() const { return r; }
};

// --- double& member ---
struct BoxDouble {
	const double& d;
	BoxDouble(const double& v) : d(v) {}
	double get() const { return d; }
};

// --- float& member ---
struct BoxFloat {
	const float& f;
	BoxFloat(const float& v) : f(v) {}
	float get() const { return f; }
};

struct WrapFloat {
	float& f;
	WrapFloat(float& v) : f(v) {}
	void twice() { f = f * 2.0f; }
};

// --- short& member ---
struct BoxShort {
	const short& s;
	BoxShort(const short& v) : s(v) {}
	short get() const { return s; }
};

// --- char& member ---
struct BoxChar {
	const char& c;
	BoxChar(const char& v) : c(v) {}
	char get() const { return c; }
};

// --- long long& member ---
struct BoxLL {
	const long long& v;
	BoxLL(const long long& x) : v(x) {}
	long long get() const { return v; }
};

// --- enum& member ---
enum Color { Red = 1,
			 Green = 2,
			 Blue = 4 };

struct BoxEnum {
	const Color& c;
	BoxEnum(const Color& v) : c(v) {}
	Color get() const { return c; }
};

// --- struct (const read through reference member) ---
struct Point {
	int x;
	int y;
};

struct BoxPoint {
	const Point& p;
	BoxPoint(const Point& v) : p(v) {}
	int sumXY() const { return p.x + p.y; }
};

// --- mutable struct reference member: field read + write through method ---
struct WrapPoint {
	Point& p;
	WrapPoint(Point& v) : p(v) {}
	void scale(int n) {
		p.x = p.x * n;
		p.y = p.y * n;
	}
	int getX() const { return p.x; }
	int getY() const { return p.y; }
};

// --- Pre-bind conversions: ctor(const T&) called with convertible source ---
struct SinkDouble {
	double stored;
	SinkDouble(const double& x) : stored(x) {}
};

struct SinkFloat {
	float stored;
	SinkFloat(const float& x) : stored(x) {}
};

int main() {
 // Part 1: reading through const int& member
	int a = 42;
	BoxInt b(a);
	if (b.get() != 42)
		return 1;

 // Part 2: mutation must propagate back through non-const int& member
	int c = 10;
	Wrapper w(c);
	w.inc();
	if (c != 11)
		return 2;  // caller sees the mutation
	if (w.get() != 11)
		return 3;
	w.add(5);
	if (c != 16)
		return 4;
	if (w.get() != 16)
		return 5;

 // Part 3: reading through const double& member
	double dd = 3.14;
	BoxDouble bd(dd);
	if ((int)(bd.get() * 100) != 314)
		return 6;

 // Part 4: constructor pre-bind conversion int → const double&
	SinkDouble sd(7);
	if ((int)sd.stored != 7)
		return 7;

 // Part 5: const int& member with int literal arg (temporary materialization)
	BoxInt lit(99);
	if (lit.get() != 99)
		return 8;

 // Part 6: float reference member (read)
	float fv = 1.5f;
	BoxFloat bf(fv);
	if ((int)(bf.get() * 2) != 3)
		return 9;

 // Part 7: float reference member (mutate)
	float mf = 4.0f;
	WrapFloat wf(mf);
	wf.twice();
	if ((int)mf != 8)
		return 10;

 // Part 8: short reference member
	short sv = 200;
	BoxShort bs(sv);
	if (bs.get() != 200)
		return 11;

 // Part 9: char reference member
	char cv = 'Z';
	BoxChar bc(cv);
	if (bc.get() != 'Z')
		return 12;

 // Part 10: long long reference member
	long long llv = 1000000000LL;
	BoxLL bll(llv);
	if (bll.get() != 1000000000LL)
		return 13;

 // Part 11: enum reference member
	Color col = Green;
	BoxEnum be(col);
	if (be.get() != Green)
		return 14;

 // Part 12: const struct reference member (read)
	Point pt{3, 7};
	BoxPoint bp(pt);
	if (bp.sumXY() != 10)
		return 15;

 // Part 13: constructor pre-bind conversion int → const float&
	SinkFloat sf(3);
	if ((int)sf.stored != 3)
		return 16;

 // Part 14: mutable struct reference member — read fields through method
	Point mp{2, 5};
	WrapPoint wp(mp);
	if (wp.getX() != 2)
		return 17;
	if (wp.getY() != 5)
		return 18;

 // Part 15: mutable struct reference member — write fields through method
	wp.scale(3);
	if (mp.x != 6)
		return 19;
	if (mp.y != 15)
		return 20;

 // Part 16: mutable struct reference member — verify wrapper reads updated values
	if (wp.getX() != 6)
		return 21;
	if (wp.getY() != 15)
		return 22;

	return 0;
}
