/* join_code.c: the 6- and 8-symbol forms, dotted addresses, rejections.
 * Expected codes were computed independently (Python, the same alphabet). */
#include "online/join_code.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static join_endpoint ep(int a, int b, int c, int d, unsigned port)
{
    join_endpoint e;
    e.ip[0] = (uint8_t)a; e.ip[1] = (uint8_t)b; e.ip[2] = (uint8_t)c; e.ip[3] = (uint8_t)d;
    e.port = (uint16_t)port;
    return e;
}

static int same(const join_endpoint *x, const join_endpoint *y)
{
    return !memcmp(x->ip, y->ip, 4) && x->port == y->port;
}

int main(void)
{
    char code[JOIN_CODE_LEN];
    join_endpoint e, back;

    /* 6 symbols: a port inside 7000..7015 */
    e = ep(203, 0, 113, 7, 7003);
    CHECK(join_code_make(&e, 1, code) == 1);
    CHECK(strcmp(code, "ywBxBz") == 0);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));
    e = ep(203, 0, 113, 7, 7015);                                  /* the last indexed port */
    CHECK(join_code_make(&e, 1, code) == 1 && strcmp(code, "ywBxB_") == 0);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));

    /* 8 symbols: a rewritten port, either side of the range */
    e = ep(198, 51, 100, 20, 49152);
    CHECK(join_code_make(&e, 1, code) == 1 && strcmp(code, "xjNkFMAA") == 0);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));
    e = ep(198, 51, 100, 20, 7016);
    CHECK(join_code_make(&e, 1, code) == 1 && strlen(code) == 8);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));
    e = ep(198, 51, 100, 20, 6999);
    CHECK(join_code_make(&e, 1, code) == 1 && strlen(code) == 8);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));

    /* case-sensitive: one flipped letter is another address */
    e = ep(203, 0, 113, 7, 7003);
    CHECK(join_code_parse("YwBxBz", 7000, &back, 1) == 1 && !same(&e, &back));
    /* blanks around and inside are ignored */
    CHECK(join_code_parse("  ywB xBz \r\n", 7000, &back, 1) == 1 && same(&e, &back));

    /* dotted addresses */
    CHECK(join_code_parse("192.168.1.5", 7004, &back, 1) == 1);
    e = ep(192, 168, 1, 5, 7004);
    CHECK(same(&e, &back));
    CHECK(join_code_parse("192.168.1.5:7001", 7004, &back, 1) == 1 && back.port == 7001);
    CHECK(join_code_parse("127.0.0.1:7001 ", 7000, &back, 1) == 1 && back.ip[0] == 127);

    /* rejections */
    CHECK(join_code_parse("ywBxB", 7000, &back, 1) == 0);           /* 5 symbols */
    CHECK(join_code_parse("ywBxBzA", 7000, &back, 1) == 0);         /* 7 */
    CHECK(join_code_parse("ywBxBzAAA", 7000, &back, 1) == 0);       /* 9 */
    CHECK(join_code_parse("ywB!Bz", 7000, &back, 1) == 0);          /* not in the alphabet */
    CHECK(join_code_parse("AAAAAA", 7000, &back, 1) == 0);          /* 0.0.0.0 */
    CHECK(join_code_parse("xjNkFAAA", 7000, &back, 1) == 0);        /* 8 symbols, port 0 */
    CHECK(join_code_parse("192.168.1.5:0", 7000, &back, 1) == 0);
    CHECK(join_code_parse("256.1.1.1", 7000, &back, 1) == 0);
    CHECK(join_code_parse("192.168.1.5:70000", 7000, &back, 1) == 0);
    CHECK(join_code_parse("", 7000, &back, 1) == 0);
    CHECK(join_code_parse(NULL, 7000, &back, 1) == 0);
    e = ep(1, 2, 3, 4, 0);
    CHECK(join_code_make(&e, 1, code) == 0 && code[0] == 0);       /* nothing to encode */

    printf("join_code_test: %d failure(s)\n", failures);
    return failures;
}
