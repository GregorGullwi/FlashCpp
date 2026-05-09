// Test: round-trip template-template arguments through TypeInfo::TemplateArgInfo
// Verifies that TemplateArgInfo preserves template name for template-template arguments

// Template to be used as a template-template argument
template<class T>
struct Box {
T value;
static int test() { return 0; }
};

// Another template for comparison
template<class T>
struct Container {
T item;
static int test() { return 0; }
};

// Template that takes a template-template parameter
template<template<class> class Tmpl, class T>
struct TemplateApplier {
Tmpl<T> result;
static int test() { return 0; }
};

// Explicit instantiation with template-template argument
template class TemplateApplier<Box, int>;
template class TemplateApplier<Container, int>;

int main() {
TemplateApplier<Box, int>::test();
TemplateApplier<Container, int>::test();
return 0;
}
