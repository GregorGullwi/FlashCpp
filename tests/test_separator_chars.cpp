// Test that separators work correctly with macro expansion
#define ABC 123
#define XYZ 456

// Test with various separators
#if ABC&&XYZ          // && should be separator
#endif
#if ABC||XYZ          // || should be separator  
#endif
#if ABC+XYZ           // + should be separator
#endif
#if ABC-XYZ           // - should be separator
#endif
#if ABC*XYZ           // * should be separator
#endif
#if ABC/XYZ           // / should be separator
#endif
#if ABC<XYZ           // < should be separator
#endif
#if ABC>XYZ           // > should be separator
#endif
#if ABC==XYZ          // = should be separator
#endif
#if ABC!=XYZ          // ! should be separator
#endif
#if ABC&XYZ           // & should be separator
#endif
#if ABC|XYZ           // | should be separator
#endif
#if ABC^XYZ           // ^ should be separator
#endif
#if ABC%XYZ           // % should be separator
#endif
#if (ABC)             // ( ) should be separators
#endif
#if ABC,XYZ           // , should be separator (in macro args)
#endif

int main() {
    return 0;
}
