// Test that separators work correctly with macro expansion
#define ABC 123
#define XYZ 456

// Test with various separators
#if ABC && XYZ		   // && should be separator
#endif
#if ABC || XYZ		   // || should be separator
#endif
#if ABC + XYZ			  // + should be separator
#endif
#if ABC - XYZ			  // - should be separator
#endif
#if ABC * XYZ			  // * should be separator
#endif
#if ABC / XYZ			  // / should be separator
#endif
#if ABC < XYZ			  // < should be separator
#endif
#if ABC > XYZ			  // > should be separator
#endif
#if ABC == XYZ		   // = should be separator
#endif
#if ABC != XYZ		   // ! should be separator
#endif
#if ABC & XYZ			  // & should be separator
#endif
#if ABC | XYZ			  // | should be separator
#endif
#if ABC ^ XYZ			  // ^ should be separator
#endif
#if ABC % XYZ			  // % should be separator
#endif
#if (ABC)			 // ( ) should be separators
#endif
#define ADD_PAIR(a, b) ((a) + (b))
#if ADD_PAIR(ABC, XYZ)	 // , is valid as a macro-argument separator
#endif

int main() {
	return 0;
}
