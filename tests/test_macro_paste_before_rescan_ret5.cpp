// Test: ## token-pasting must happen before rescanning (C standard 6.10.3.3)
// When a macro body contains ## and the result of pasting creates a new 
// identifier, that identifier should be available for further macro expansion.

#define PASTE(a, b) a ## b
#define INDIRECT_PASTE(a, b) PASTE(a, b)

// INDIRECT_PASTE should:
// 1. Expand args (not adjacent to ## in INDIRECT_PASTE): a=get, b=_five
// 2. Substitute into PASTE(get, _five)
// 3. In PASTE: a=get, b=_five adjacent to ##, not pre-expanded
// 4. ## processing: get ## _five -> get_five
// 5. Rescan: get_five is a function name, not a macro -> done

int get_five() { return 5; }

int main() {
    return INDIRECT_PASTE(get, _five)();
}
