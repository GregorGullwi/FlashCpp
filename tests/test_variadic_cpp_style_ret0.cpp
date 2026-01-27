// Test C++ style variadic function declarations (without extern "C")

// Variadic function with C++ linkage
int format_string(char* buffer, const char* format, ...);

// Variadic function with multiple typed parameters
void debug_log(int level, const char* category, const char* message, ...);

// Class with variadic member function (declaration only)
class Logger {
public:
    // Note: Variadic member functions are rare but valid in C++
    void log(const char* format, ...);
    
    // Non-variadic member function for comparison
    void setLevel(int level);
};

int main() {
    // Just test compilation of declarations
    return 0;
}

