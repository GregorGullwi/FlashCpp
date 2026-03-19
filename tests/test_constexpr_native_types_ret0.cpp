// Regression test for constexpr support of all native primitive types.
// Covers: direct variables, arithmetic, comparisons, and mixed-type operations
// for bool, char, short, int, long, long long, their unsigned variants,
// float, double, and long double.

// --- Direct constexpr variables ---
constexpr bool  cv_bool   = true;                           static_assert(cv_bool   == true);
constexpr char  cv_char   = 'A';                            static_assert(cv_char   == 65);
constexpr signed char  cv_schar  = -5;                      static_assert(cv_schar  == -5);
constexpr unsigned char  cv_uchar  = 200;                   static_assert(cv_uchar  == 200);
constexpr short cv_short  = 1000;                           static_assert(cv_short  == 1000);
constexpr unsigned short cv_ushort = 60000;                 static_assert(cv_ushort == 60000);
constexpr int   cv_int    = -42;                            static_assert(cv_int    == -42);
constexpr unsigned int   cv_uint   = 4000000000u;           static_assert(cv_uint   == 4000000000u);
constexpr long  cv_long   = 100000L;                        static_assert(cv_long   == 100000L);
constexpr unsigned long  cv_ulong  = 3000000000UL;          static_assert(cv_ulong  == 3000000000UL);
constexpr long long  cv_llong  = -9000000000LL;             static_assert(cv_llong  == -9000000000LL);
constexpr unsigned long long cv_ullong = 10000000000ULL;    static_assert(cv_ullong == 10000000000ULL);
constexpr float  cv_float  = 3.14f;                         static_assert(cv_float  == 3.14f);
constexpr double cv_double = 2.718281828;                   static_assert(cv_double == 2.718281828);
constexpr long double cv_ldouble = 1.41421356L;             static_assert(cv_ldouble == 1.41421356L);

// --- Arithmetic (same-type) ---
constexpr double  da = 3.14 + 2.0;    static_assert(da > 5.0);
constexpr float   fa = 3.14f + 2.0f;  static_assert(fa > 5.0f);
constexpr unsigned int  ua = 100u + 200u;   static_assert(ua == 300u);
constexpr long long     la = 1000000LL * 1000000LL;  static_assert(la == 1000000000000LL);

// --- Comparisons ---
static_assert(3.14  > 3.0);     // double > double (non-integer value)
static_assert(3.14f > 3.0f);    // float  > float
static_assert(3.0   >= 3.0);    // double >=
constexpr unsigned long long cv_big = 18000000000000000000ULL;
static_assert(cv_big > 1ULL);   // unsigned long long > 1 (value beyond LLONG_MAX)

// --- Functions returning non-int types ---
constexpr char fn_char(char a) { return a; }
static_assert(fn_char('Z') == 90);

constexpr double fn_double(double a, double b) { return a * b; }
static_assert(fn_double(3.0, 2.0) == 6.0);
static_assert(fn_double(3.14, 2.0) > 6.0);   // 6.28 > 6.0

constexpr float fn_float(float a) { return a + 1.0f; }
static_assert(fn_float(2.5f) > 3.0f);         // 3.5 > 3.0

constexpr unsigned long long fn_ull(unsigned long long a) { return a + 1ULL; }
static_assert(fn_ull(18000000000000000000ULL) == 18000000000000000001ULL);

// --- C-style and static_cast inside constexpr functions ---
constexpr int fn_ccast(double a)        { return (int)a; }
static_assert(fn_ccast(3.9) == 3);     // truncation

constexpr double fn_ccast2(int a)       { return (double)a; }
static_assert(fn_ccast2(5) == 5.0);

constexpr int fn_scast(double a) { return static_cast<int>(a); }
static_assert(fn_scast(7.9) == 7);

// --- Mixed-type arithmetic (usual arithmetic conversions) ---
// float + int → double path
constexpr float cv_f = 3.5f;
constexpr int   cv_i = 2;
constexpr float fi_add = cv_f + cv_i;
static_assert(fi_add > 5.0f);         // 5.5 > 5.0
static_assert(cv_f   > cv_i);         // 3.5 > 2

// int + bool → signed path
constexpr bool cv_yes = true;
constexpr int  nb = cv_i + cv_yes;    // 2 + 1 = 3
static_assert(nb == 3);
static_assert(cv_i > cv_yes);         // 2 > 1

// unsigned long long + signed long long → unsigned path
constexpr long long  cv_one = 1LL;
constexpr unsigned long long cv_sum = cv_big + cv_one;
static_assert(cv_sum == 18000000000000000001ULL);
static_assert(cv_big > cv_one);       // unsigned path: 18e18 > 1

// double + bool → double path
constexpr double db = 2.5 + cv_yes;   // 2.5 + 1.0 = 3.5
static_assert(db == 3.5);

// int + double literal → double path
constexpr double mixed = 3 + 0.14;
static_assert(mixed > 3.0 && mixed < 4.0);

constexpr double weighted(int count, double weight) { return count * weight; }
constexpr double wt = weighted(3, 2.5);
static_assert(wt == 7.5);

int main() { return 0; }
