// Test: Pointer-type member with brace default member initializer inside a
// member struct template body. This mirrors the MSVC STL
// transform_view::_Iterator pattern:
//   _Parent_t* _Parent{};
// which previously failed with "Expected '(' or ';' after member declaration".

struct outer {
	template <bool Const>
	struct iterator {
		int* current{};
		outer* parent{};
	};
};

int main() {
	outer::iterator<true> it;
	(void)it;
	return 0;
}