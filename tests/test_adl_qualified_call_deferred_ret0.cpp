// Test: ADL participation tracking - verify that when ordinary lookup finds a
// blocking non-function declaration, ADL is correctly suppressed and the lookup
// record does not falsely record argument_dependent_lookup_included.
// This tests that in a template body, the replay of the lookup record is accurate.
namespace Lib {
struct Tag {};
int run(Tag) { return 0; }
}

// A variable 'run' blocks ADL for 'run' in the global namespace
int run_int = 0;

template<typename T>
int callRun(Lib::Tag t) {
return Lib::run(t);
}

int main() {
Lib::Tag t;
return callRun<int>(t);  // 0
}
