namespace std {
	namespace ranges {
		namespace _Swap {
			template<typename Type>
			concept movable = true;

			struct _Cpo {
				template<typename Type>
					requires movable<Type>
				void operator()(Type& left, Type& right) const {
					Type temp = left;
					left = right;
					right = temp;
				}
			};
		}

		inline constexpr _Swap::_Cpo swap{};
	}
}

int main() {
	int left = 1;
	int right = 2;
	std::ranges::swap(left, right);
	return left == 2 && right == 1 ? 0 : 1;
}
