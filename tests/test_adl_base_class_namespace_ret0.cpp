namespace Geom {
    struct Shape { int id = 0; };
    int describe(Shape s) { return s.id; }
}

struct Circle : Geom::Shape { int radius; };

int main() {
    Circle c;
    c.id = 0;
    return describe(c);  // ADL: Circle derives from Geom::Shape, so Geom is associated
}
