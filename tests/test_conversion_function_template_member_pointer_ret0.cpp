// Conversion-function templates must preserve pointer-to-member owner identity
// and rank member-object, member-function, and free-function pointer targets.

struct Owner {
	int data;
	int run();
};

using DataPointer = int Owner::*;
using FunctionPointer = int (Owner::*)();
using FreeFunctionPointer = int (*)(int);

struct TemplateSource {
	template<class T>
	operator T() const { return T(); }
};

struct DataSelection { char value; };
struct FunctionSelection { char value; };
struct FreeFunctionSelection { char value; };
struct EllipsisSelection { long long value; };

DataSelection select(DataPointer);
EllipsisSelection select(...);
FunctionSelection select_function(FunctionPointer);
EllipsisSelection select_function(...);
FreeFunctionSelection select_free(FreeFunctionPointer);
EllipsisSelection select_free(...);

static_assert(__is_same(decltype(select(TemplateSource{})), DataSelection));
static_assert(__is_same(decltype(select_function(TemplateSource{})), FunctionSelection));
static_assert(__is_same(decltype(select_free(TemplateSource{})), FreeFunctionSelection));

struct ExactSource {
	operator DataPointer() const { return &Owner::data; }
	template<class T>
	operator T() const { return T(); }
};

int main() {
	TemplateSource template_source;
	DataPointer data = template_source;
	FunctionPointer function = template_source;
	FreeFunctionPointer free_function = template_source;
	if (data != nullptr || function != nullptr || free_function != nullptr) {
		return 1;
	}
	ExactSource exact_source;
	DataPointer exact = exact_source;
	return exact == &Owner::data ? 0 : 2;
}
