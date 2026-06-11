// Regression: dependent unqualified call completion can materialize a resolved
// direct call that preserves the authoritative mangled target while losing the
// original lookup record. Sema must still recover that bound target instead of
// rebinding to a later ordinary overload.

int lookup_probe(long) {
	return 1;
}

template <typename T>
int use_dependent_lookup() {
	return lookup_probe(T{});
}

int lookup_probe(int) {
	return 2;
}

int main() {
	return use_dependent_lookup<int>() - 1;
}
