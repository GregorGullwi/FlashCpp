// Deleted general constructors must survive class-template instantiation with
// their parameter types substituted, so a deleted best match rejects
// constructibility in the specialization.

template<class T> struct TemplateDeletedInt {
	TemplateDeletedInt(int) = delete;
	TemplateDeletedInt(char);
};

template<class T> struct TemplateDeletedParam {
	TemplateDeletedParam(T) = delete;
	TemplateDeletedParam(char);
};

static_assert(!__is_constructible(TemplateDeletedInt<double>, int));
static_assert(__is_constructible(TemplateDeletedInt<double>, char));

static_assert(!__is_constructible(TemplateDeletedParam<int>, int));
static_assert(__is_constructible(TemplateDeletedParam<int>, char));

int main() {
	return 0;
}
