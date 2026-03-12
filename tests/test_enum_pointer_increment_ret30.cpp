// Test that ++/-- on enum pointers uses pointer-stride arithmetic
// (advances by sizeof(enum), not by 1 byte).
//
// This exercises the code path in generateBuiltinIncDec where
// getOperandPointerDepth must correctly report pointer_depth > 0
// for enum pointer types. A bug in toExprResult causes it to
// misinterpret slot-4 as type_index instead of pointer_depth for
// Type::Enum, resulting in integer increment instead of pointer
// arithmetic.

enum Color { Red = 0, Green = 10, Blue = 20 };

int main() {
    Color arr[3];
    arr[0] = Red;
    arr[1] = Green;
    arr[2] = Blue;

    Color* p = &arr[0];

    // Prefix increment: should advance p by sizeof(Color) = 4 bytes
    // to point to arr[1] (Green = 10)
    ++p;
    int val1 = *p;  // Expected: 10

    // Postfix increment: should advance p by sizeof(Color) = 4 bytes
    // to point to arr[2] (Blue = 20), but return old value (arr[1])
    Color* old = p++;
    int val2 = *old;  // Expected: 10 (old position)
    int val3 = *p;    // Expected: 20 (new position)

    // If pointer arithmetic is wrong (integer increment by 1 byte
    // instead of sizeof(Color) = 4 bytes), dereferencing will read
    // garbage or the wrong element.

    // val1 + val3 = 10 + 20 = 30
    return val1 + val3;
}
