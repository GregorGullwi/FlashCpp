using Row = short[3];

template<int Rows>
using Grid = Row[Rows];

Grid<0> invalid;
