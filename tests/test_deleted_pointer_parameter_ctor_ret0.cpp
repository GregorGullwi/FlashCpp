// A deleted constructor whose parameter is a pointer to its own class must
// participate in constructibility even though the class record is still being
// published when the constructor schema is emitted.

struct DeletedPointer {
	DeletedPointer();
	DeletedPointer(DeletedPointer* const&) = delete;
};

static_assert(!__is_constructible(DeletedPointer, DeletedPointer*));
static_assert(!__is_nothrow_constructible(DeletedPointer, DeletedPointer*));
static_assert(__is_constructible(DeletedPointer));

int main() {
	return 0;
}
