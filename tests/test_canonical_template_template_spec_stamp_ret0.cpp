// Published primary class templates used as template-template arguments must
// retain their TemplateDeclId when a template-id is stamped for canonical
// import. Packs, aliases, and unpublished template parameters stay deferred.
template <typename T>
struct Unary {
	using type = T;
};

template <template <typename> class F, typename T>
struct Apply {
	static constexpr int size = sizeof(typename F<T>::type);
};

int main() {
	return Apply<Unary, char>::size - 1;
}
