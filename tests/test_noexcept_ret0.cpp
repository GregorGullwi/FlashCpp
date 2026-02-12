// Test noexcept specifier on functions

int safe_function() noexcept {
    return 42;
}

int conditional_noexcept(bool b) noexcept(false) {
    if (b) {
        return 100;
    }
    throw 1;
}

int main() {
    int result = safe_function();
    
    int result2 = conditional_noexcept(true);
    
	int result3 = 0;
    try {
        conditional_noexcept(false);
		result = 2;
    } catch (int e) {
        result3 = e;
    }
    
    return result + result2 + result3 == 143 ? 0 : 1;
}
