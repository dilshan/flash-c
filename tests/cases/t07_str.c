#include <stdio.h>
#include <string.h>
#include <ctype.h>
void rev(char *s) { int i = 0, j = strlen(s) - 1; while (i < j) { char t = s[i]; s[i++] = s[j]; s[j--] = t; } }
int count_words(const char *s)
{
    int n = 0, in = 0;
    for (; *s; s++) {
        if (isspace(*s)) in = 0;
        else if (!in) { in = 1; n++; }
    }
    return n;
}
int main()
{
    char s[64] = "The quick brown fox";
    char t[64];
    int i;
    printf("%d\n", count_words(s));
    strcpy(t, s); rev(t);
    printf("%s\n", t);
    for (i = 0; s[i]; i++) s[i] = toupper(s[i]);
    printf("%s\n", s);
    printf("%d %d\n", !!isdigit('5'), !!isalpha('5'));
    printf("[%s]\n", strchr(s, 'Q'));
    printf("[%s]\n", strrchr(s, 'O'));
    char *tok = strtok(t, " ");
    while (tok) { printf("<%s>", tok); tok = strtok(NULL, " "); }
    printf("\n");
    printf("%d %d\n", (int)strlen(""), strncmp("abcd", "abxx", 2));
    char c = 'a'; c++;
    printf("%c %d\n", c, c);
    printf("%s\n", "multi" "part" " string");
    printf("tab\there \"quoted\" \\ back \x41 \101\n");
    return 0;
}
