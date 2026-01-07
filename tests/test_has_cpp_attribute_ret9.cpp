// Verify __has_cpp_attribute preprocessor support for common C++20 attributes

#if __has_cpp_attribute(deprecated) == 201309
int has_deprecated = 1;
#else
int has_deprecated = 0;
#endif

#if __has_cpp_attribute(fallthrough) == 201603
int has_fallthrough = 1;
#else
int has_fallthrough = 0;
#endif

#if __has_cpp_attribute(likely) == 201803
int has_likely = 1;
#else
int has_likely = 0;
#endif

#if __has_cpp_attribute(unlikely) == 201803
int has_unlikely = 1;
#else
int has_unlikely = 0;
#endif

#if __has_cpp_attribute(maybe_unused) == 201603
int has_maybe_unused = 1;
#else
int has_maybe_unused = 0;
#endif

#if __has_cpp_attribute(no_unique_address) == 201803
int has_no_unique_address = 1;
#else
int has_no_unique_address = 0;
#endif

#if __has_cpp_attribute(nodiscard) == 201907
int has_nodiscard = 1;
#else
int has_nodiscard = 0;
#endif

#if __has_cpp_attribute(noreturn) == 200809
int has_noreturn = 1;
#else
int has_noreturn = 0;
#endif

#if __has_cpp_attribute(not_an_attribute) == 0
int has_unknown = 1;
#else
int has_unknown = 0;
#endif

int main() {
	// Sum of attribute checks verifies __has_cpp_attribute results for all cases above
	return has_deprecated + has_fallthrough + has_likely +
		has_unlikely + has_maybe_unused + has_no_unique_address +
		has_nodiscard + has_noreturn + has_unknown;
}
