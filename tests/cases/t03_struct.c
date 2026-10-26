#include <stdio.h>
#include <stdlib.h>
struct point { int x, y; };
struct rect { struct point a, b; char name[8]; };
union u { int i; char c[4]; };
typedef struct node { int val; struct node *next; } Node;
enum color { RED, GREEN = 5, BLUE };
int area(struct rect *r) { return (r->b.x - r->a.x) * (r->b.y - r->a.y); }
int main()
{
    struct rect r = { {1, 2}, {5, 8}, "box" };
    struct point p = { 3, 4 }, q;
    union u un;
    Node *head = NULL, *n;
    int i;
    q = p;
    q.x += 10;
    printf("%d %d %d %d\n", p.x, p.y, q.x, q.y);
    printf("%d %s\n", area(&r), r.name);
    un.i = 0x01020304;
    printf("%d %d\n", un.c[0], un.c[3]);
    for (i = 0; i < 5; i++) { n = malloc(sizeof(Node)); n->val = i; n->next = head; head = n; }
    for (n = head; n; n = n->next) printf("%d ", n->val);
    printf("\n");
    printf("%d %d %d\n", RED, GREEN, BLUE);
    enum color c = BLUE;
    printf("%d\n", c == BLUE);
    printf("%d\n", (int)sizeof(struct rect));
    struct point pts[3] = { {1,2}, {3,4}, {5,6} };
    printf("%d\n", pts[1].y + pts[2].x);
    return 0;
}
