// Test: macro argument pre-expansion before substitution
// Per C standard 6.10.3.1: arguments not adjacent to # or ## are expanded 
// before substitution into the replacement list

#define INNER_CONCAT(x, y) x ## y
#define CONCAT(x, y) INNER_CONCAT(x, y)
#define MAKE_NAME(prefix, suffix) CONCAT(prefix, suffix)

// Two-level indirection: WRAPPER calls MAKE_NAME, which calls CONCAT
#define CALL_FN(prefix, suffix) MAKE_NAME(prefix, suffix)()
#define GET_SUFFIX() _val

// CALL_FN(get, GET_SUFFIX()) should:
// 1. Pre-expand GET_SUFFIX() to _val (argument not adjacent to ## in CALL_FN)
// 2. Substitute into MAKE_NAME(get, _val)
// 3. MAKE_NAME calls CONCAT(get, _val)
// 4. CONCAT produces INNER_CONCAT(get, _val) -> get_val
// 5. () makes it a function call
int get_val() { return 3; }

int main() {
    return CALL_FN(get, GET_SUFFIX());
}
