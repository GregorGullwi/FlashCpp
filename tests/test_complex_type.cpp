// Test _Complex type support (C99/C11 extension)
// FlashCpp treats _Complex as a type modifier and strips it (complex arithmetic not yet supported)

typedef _Complex float cfloat;
typedef _Complex double cdouble;

// GCC-style typedef with __attribute__ and __mode__
typedef _Complex float __cfloat128 __attribute__ ((__mode__ (__TC__)));

int main() {
	// We're just testing that these typedefs parse correctly
	// Complex arithmetic is not yet supported in FlashCpp
	return 0;
}
