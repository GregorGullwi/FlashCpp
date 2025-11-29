// Test: recursive macro that stays as identifier
#define RECUR RECUR

// This will expand RECUR -> RECUR (stops due to recursion prevention)
// Parser will see "RECUR" which isn't a valid type
// This is CORRECT behavior - the macro system is working
// In real MSVC SAL headers, the macros expand to empty or valid attributes

int RECUR = 10;  // Parser will complain: "int RECUR = 10" - RECUR is not resolved

int main() {
    return 0;
}
