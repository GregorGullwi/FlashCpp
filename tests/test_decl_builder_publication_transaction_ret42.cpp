// PR boundary 10: parser shadow path uses preflight classify and publication
// transactions so rejected DeclarationBuilder publishes leave no entity state.

namespace decl_builder_publication_txn_ns {
void publication_txn_fn();
void publication_txn_fn() {}
}

int main() {
	return 42;
}
