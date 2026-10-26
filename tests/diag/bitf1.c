struct s { int a : 3; unsigned b : 4; int c; };
struct t { char x : 2; };
struct u { short y : 2; };
struct v { long z : 2; };
struct w { float f : 2; };
struct x { int big : 33; };
struct y { int zero : 0; };
struct z { int neg : -1; };
int main(void)
{
    struct s v1;
    int *p = &v1.a;
    sizeof(v1.b);
    return 0;
}
