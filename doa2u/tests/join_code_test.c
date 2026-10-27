/* join_code.c: the 6- and 8-symbol forms (scrambled, so a code does not show
 * the address), dotted addresses, rejections. */
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
    char code[JOIN_CODE_LEN], code2[JOIN_CODE_LEN];
    join_endpoint e, back;
    unsigned i, differ, total;

    /* 6 symbols: a port inside 7000..7015; the code round-trips */
    e = ep(203, 0, 113, 7, 7003);
    CHECK(join_code_make(&e, 1, code) == 1 && strlen(code) == 6);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));
    e = ep(203, 0, 113, 7, 7015);                                  /* the last indexed port */
    CHECK(join_code_make(&e, 1, code) == 1 && strlen(code) == 6);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));

    /* 8 symbols: a rewritten port, either side of the range */
    e = ep(198, 51, 100, 20, 49152);
    CHECK(join_code_make(&e, 1, code) == 1 && strlen(code) == 8);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));
    e = ep(198, 51, 100, 20, 7016);
    CHECK(join_code_make(&e, 1, code) == 1 && strlen(code) == 8);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));
    e = ep(198, 51, 100, 20, 6999);
    CHECK(join_code_make(&e, 1, code) == 1 && strlen(code) == 8);
    CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));

    /* the code does not show the address: not the plain base64 of it
     * (203.0.113.7:7003 was "ywBxBz", 198.51.100.20:49152 "xjNkFMAA") */
    e = ep(203, 0, 113, 7, 7003);
    join_code_make(&e, 1, code);
    CHECK(strcmp(code, "ywBxBz") != 0);
    e = ep(198, 51, 100, 20, 49152);
    join_code_make(&e, 1, code);
    CHECK(strcmp(code, "xjNkFMAA") != 0);
    /* neighbouring addresses give unrelated codes: on average most symbols
     * differ, where the plain encoding changed only the last one or two */
    differ = total = 0;
    for (i = 1; i < 200; i++) {
        join_endpoint a = ep(192, 168, 1, (int)i, 7000), b = ep(192, 168, 1, (int)i + 1, 7000);
        join_code_make(&a, 1, code);
        join_code_make(&b, 1, code2);
        for (int k = 0; k < 6; k++) differ += code[k] != code2[k];
        total += 6;
    }
    CHECK(differ * 2 > total);
    /* and every one of many addresses still round-trips */
    for (i = 0; i < 5000; i++) {
        unsigned v = i * 2654435761u;
        unsigned port = (i & 1) ? 7000 + (i % 16) : 1024 + (i * 7919u) % 64000;
        e = ep((int)(v >> 24) | 1, (int)(v >> 16) & 255, (int)(v >> 8) & 255, (int)v & 255, port);
        CHECK(join_code_make(&e, 1, code) == 1);
        CHECK(join_code_parse(code, 7000, &back, 1) == 1 && same(&e, &back));
    }

    /* case-sensitive: one flipped letter is another address (or none) */
    e = ep(203, 0, 113, 7, 7003);
    join_code_make(&e, 1, code);
    strcpy(code2, code);
    code2[0] = (code2[0] >= 'a' && code2[0] <= 'z') ? (char)(code2[0] - 32)
             : (code2[0] >= 'A' && code2[0] <= 'Z') ? (char)(code2[0] + 32) : (code2[0] == '0' ? '1' : '0');
    CHECK(join_code_parse(code2, 7000, &back, 1) == 0 || !same(&e, &back));
    /* blanks around and inside are ignored */
    {
        char spaced[32];
        _snprintf(spaced, sizeof spaced, "  %.3s %.3s \r\n", code, code + 3);
        CHECK(join_code_parse(spaced, 7000, &back, 1) == 1 && same(&e, &back));
    }

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
    {   /* the codes of 0.0.0.0 and of port 0 are refused */
        join_code_text_of(0, 6, code);  CHECK(join_code_parse(code, 7000, &back, 1) == 0);
        join_code_text_of((uint64_t)0x01020304u << 16, 8, code); CHECK(join_code_parse(code, 7000, &back, 1) == 0);
    }
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
