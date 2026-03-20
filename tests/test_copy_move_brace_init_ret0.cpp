// Test: struct with both copy and move ctors, copy from lvalue correctly
// selects the copy ctor (not the move ctor).
// Exercises the is_lvalue_reference() fix in copy ctor detection paths.

struct Tracker {
    int val;
    int copy_flag;
    int move_flag;
    Tracker(int v) : val(v), copy_flag(0), move_flag(0) {}
    Tracker(const Tracker& other) : val(other.val), copy_flag(1), move_flag(0) {}
    Tracker(Tracker&& other) : val(other.val), copy_flag(0), move_flag(1) {}
};

int main() {
    Tracker a(10);
    Tracker b = a;   // copy construction from lvalue => should use copy ctor
    if (b.move_flag) return 1;   // should NOT be moved
    if (b.val != 10) return 2;
    if (b.copy_flag != 1) return 3;  // should be copied
    return 0;
}
