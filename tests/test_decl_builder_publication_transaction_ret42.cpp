// PR boundary 10: parser shadow path uses transactional publication.
// Rollback and rejection semantics are covered by FrontendContext doctests;
// this file smoke-tests declaration plus definition publication through parse.

namespace decl_builder_publication_txn_ns {
void publication_txn_fn();
void publication_txn_fn() {}
}

int main() {
	return 42;
}
