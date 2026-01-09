struct Container {
    union Data {
        int i;
        float f;
    } *data_ptr;
    
    union Data local_data;
};

int main() {
    Container c;
    c.local_data.i = 55;
    c.data_ptr = &c.local_data;
    return c.data_ptr->i;
}
