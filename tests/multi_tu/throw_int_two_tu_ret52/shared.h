// Unique out-of-line functions in two TUs both throw int. _TI1H / _CTA1H /
// _CT??_R0H@84 and ??_R0H@8 must be SELECT_ANY so the objects link.

int throwFromFirst();
int throwFromMain();
