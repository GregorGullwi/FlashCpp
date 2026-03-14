// Test enum compound assignment operations
// This tests that enum values work correctly through compound assignment
// codegen paths (handleLValueCompoundAssignment).
enum Color : int { Red = 1, Green = 2, Blue = 4 };

enum class Status : int { Pending = 0, Running = 1, Done = 2 };

int main() {
int result = 0;

// Test += with enum
int x = Red;
x += Green;
if (x != 3) return 1;  // Red + Green = 1 + 2 = 3

// Test |= with enum (bitwise OR compound)
int flags = 0;
flags |= Red;
flags |= Blue;
if (flags != 5) return 2;  // 1 | 4 = 5

// Test -= with enum
int y = Blue;
y -= Red;
if (y != 3) return 3;  // 4 - 1 = 3

// Test enum class compound assignment via static_cast
int s = static_cast<int>(Status::Running);
s += static_cast<int>(Status::Done);
if (s != 3) return 4;  // 1 + 2 = 3

return result;
}
