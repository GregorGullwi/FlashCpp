// The inline array-of-function-pointers declarator reaches the type through a
// non-template alias and an alias template. Both must instantiate to the same
// concrete element so the array is callable, including a template-dependent
// return type (T (*[2])(T)).
int inc(int x) { return x + 1; }
int dec(int x) { return x - 1; }

using InlineAlias = int (*[2])(int);
InlineAlias g_inline = {&inc, &dec};

template <class T> using TemplateAlias = T (*[2])(T);
TemplateAlias<int> g_template = {&inc, &dec};

int main() {
	InlineAlias local_inline = {&dec, &inc};
	TemplateAlias<int> local_template = {&dec, &inc};
	const bool inline_ok = g_inline[0](4) == 5 && local_inline[1](4) == 5;
	const bool template_ok = g_template[0](9) == 10 && local_template[1](9) == 10;
	return inline_ok && template_ok ? 0 : 1;
}
