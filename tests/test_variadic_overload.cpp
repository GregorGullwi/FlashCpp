// Test function overloading with variadic functions
// In C++, you can have both variadic and non-variadic versions

// Non-variadic version
int print(const char* message);

// Variadic version with same base name
int print(const char* format, ...);

// Another overload set
void log(int level);
void log(int level, const char* message);
void log(int level, const char* format, ...);

// Implementation of non-variadic version
int print(const char* message) {
	return 0;
}

// Implementation of simple log
void log(int level) {
	// Do nothing
}

void log(int level, const char* message) {
	// Do nothing
}

int main() {
	int (*print_non_variadic)(const char*) = &print;
	print_non_variadic("Hello");
	log(1);
	void (*log_non_variadic)(int, const char*) = &log;
	log_non_variadic(2, "Test");
	return 0;
}
