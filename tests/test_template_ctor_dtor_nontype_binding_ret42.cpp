// Test: instantiated ctor/dtor bodies can resolve non-type template params in sema
int gTemplateCtorDtorBinding = 0;

template <int N>
struct Counter {
	int value = 0;

	Counter() {
		value = N;
	}

	~Counter() {
		gTemplateCtorDtorBinding += N;
	}
};

int main() {
	int ctorValue = 0;
	{
		Counter<21> counter;
		ctorValue = counter.value;
	}

	return ctorValue + gTemplateCtorDtorBinding;
}
