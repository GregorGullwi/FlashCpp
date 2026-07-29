// Covers dependent member-alias materialization through a variadic class-template partial specialization.
template <class T>
struct add_const {
	using type = const T;
};
template <class T>
using add_const_t = typename add_const<T>::type;

template <class... Types>
struct Tuple;

template <unsigned Index, class TupleT>
struct Elem;

template <unsigned Index, class TupleT>
using ElemT = typename Elem<Index, TupleT>::type;

template <unsigned Index, class... Types>
ElemT<Index, Tuple<Types...>>& my_get(Tuple<Types...>& t);

template <unsigned Index, class TupleT>
struct Elem<Index, const TupleT> : Elem<Index, TupleT> {
	using Mybase = Elem<Index, TupleT>;
	using type = add_const_t<typename Mybase::type>;
};

template <class This, class... Rest>
struct Elem<0, Tuple<This, Rest...>> {
	using type = This;
	using Ttype = Tuple<This, Rest...>;
};

template <unsigned Index, class This, class... Rest>
struct Elem<Index, Tuple<This, Rest...>> : Elem<Index - 1, Tuple<Rest...>> {};

template <class This, class... Rest>
struct Tuple<This, Rest...> {
	This first;

	template <unsigned Index, class... Types>
	friend ElemT<Index, Tuple<Types...>>& my_get(Tuple<Types...>& t);
};

template <unsigned Index, class... Types>
ElemT<Index, Tuple<Types...>>& my_get(Tuple<Types...>& t) {
	using Ttype = typename Elem<Index, Tuple<Types...>>::Ttype;
	Ttype* as_ttype = static_cast<Ttype*>(&t);
	(void)as_ttype;
	return t.first;
}

int main() {
	Tuple<int, float, double> t{42};
	int& r = my_get<0>(t);
	return r == 42 ? 0 : 1;
}
