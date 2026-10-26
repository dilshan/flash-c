struct inc;
typedef int T;
typedef int T;
register int g;
const int k = 1;
const char *cp;
char *const fp = 0;
struct inc obj;
int f(int);
int h(char *);
int main(void)
{
    int i;
    k = 2;
    k++;
    *cp = 'a';
    fp = 0;
    f("x");
    h(5);
    i = 3;
    cp = "ok";
    return i;
}
