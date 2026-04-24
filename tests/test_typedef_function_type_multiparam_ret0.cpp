// Regression: C-style function-type typedef with multiple parameters must
// parse cleanly.  Pattern reproduced from glibc's
// <bits/types/cookie_io_functions_t.h>.
typedef long ssize_t;
typedef unsigned long size_t;

// typedef NAME is a function TYPE (not a function-pointer type).
typedef ssize_t cookie_read_function_t(void *cookie, char *buf, size_t nbytes);
typedef ssize_t cookie_write_function_t(void *cookie, const char *buf, size_t nbytes);

// The typedef'd function type is commonly used through a pointer.
cookie_read_function_t* g_reader = nullptr;
cookie_write_function_t* g_writer = nullptr;

int main() {
	return (g_reader == nullptr && g_writer == nullptr) ? 0 : 1;
}
