namespace std {
	template <typename Category, typename T, typename Distance = long, typename Pointer = T*, typename Reference = T&>
	struct iterator {
		using iterator_category = Category;
		using value_type = T;
		using difference_type = Distance;
		using pointer = Pointer;
		using reference = Reference;
	};

	struct random_access_iterator_tag {};

	template <typename T>
	struct iterator_traits;

	template <typename T>
	struct iterator_traits<T*> {
		using iterator_category = random_access_iterator_tag;
		using value_type = T;
		using difference_type = long;
		using pointer = T*;
		using reference = T&;
	};

	template<typename _Iterator>
	class reverse_iterator : public iterator<
		typename iterator_traits<_Iterator>::iterator_category,
		typename iterator_traits<_Iterator>::value_type,
		typename iterator_traits<_Iterator>::difference_type,
		typename iterator_traits<_Iterator>::pointer,
		typename iterator_traits<_Iterator>::reference> {
		template<typename _Iter>
		friend class reverse_iterator;

	protected:
		_Iterator current;
		typedef iterator_traits<_Iterator> __traits_type;

	public:
		typedef _Iterator iterator_type;

		reverse_iterator() : current() {}
		explicit reverse_iterator(_Iterator __x) : current(__x) {}
		reverse_iterator(const reverse_iterator& __x) : current(__x.current) {}

		template<typename _Iter>
		reverse_iterator(const reverse_iterator<_Iter>& __x) : current(__x.current) {}

		reverse_iterator operator++(int) {
			reverse_iterator __tmp = *this;
			--current;
			return __tmp;
		}
	};
}

int main() {
	int x = 5;
	std::reverse_iterator<int*> it(&x + 1);
	auto old = it++;
	old.operator++(0).operator++(0).operator++(0).operator++(0).operator++(0);
	return sizeof(old) == sizeof(int*) ? 5 : 1;
}
