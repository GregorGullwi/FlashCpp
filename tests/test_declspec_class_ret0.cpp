// Test: __declspec(dllimport) between class/struct keyword and name
// Regression test for MSVC pattern: class __declspec(dllimport) Name { ... }
class __declspec(dllimport) ImportedClass {
public:
	int value;
};

struct __declspec(dllexport) ExportedStruct {
	int x;
};

int main() { return 0; }
