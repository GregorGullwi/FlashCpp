#include <memory_resource>
#include <type_traits>

using IntAllocator = std::pmr::polymorphic_allocator<int>;
using CharAllocator = std::allocator_traits<IntAllocator>::rebind_alloc<char>;

static_assert(std::is_same_v<CharAllocator, std::pmr::polymorphic_allocator<char>>);

int main() {
	return 0;
}
