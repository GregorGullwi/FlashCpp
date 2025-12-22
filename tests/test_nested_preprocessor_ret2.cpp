// Test case for nested #ifdef/#ifndef handling
// This mirrors the structure in sal.h that was causing issues

#ifdef OUTER_UNDEFINED
  #ifndef INNER_UNDEFINED
    #define RESULT 1
  #else
    #error This should NOT trigger - we are in a skipped outer block
  #endif
#else
  #define RESULT 2
#endif

int result = RESULT;

int main() {
    return result;
}
