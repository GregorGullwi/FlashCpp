int g_dtor_calls = 0;
struct Tracked { int id; ~Tracked() { g_dtor_calls += id; } };
void thrower() { throw 42; }
int outer() {
    Tracked a{1};
    Tracked b{2};
    thrower();
    return 0;
}
int main() {
    try {
        outer();
    } catch (int e) {
        return (e == 42 && g_dtor_calls == 3) ? 0 : 1;
    }
    return 2;
}
