#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "utils.h"

#define TOKEN_KINDS \
    TOKEN_KIND(C_TOKEN_INVALID, "Invalid"), \
    TOKEN_KIND(C_TOKEN_EOF, "EOF"), \
\
TOKEN_KIND(C_TOKEN__LITERAL_BEGIN, ""), \
    TOKEN_KIND(C_TOKEN_IDENT, "identificador"), \
    TOKEN_KIND(C_TOKEN_INTEGER, "constante_int"), \
    TOKEN_KIND(C_TOKEN_FLOAT, "constante_float"), \
    TOKEN_KIND(C_TOKEN_STRING, "constante_string"), \
    TOKEN_KIND(C_TOKEN_COMMENT, "comentário"), \
TOKEN_KIND(C_TOKEN__LITERAL_END, ""), \
\
TOKEN_KIND(C_TOKEN__OPERATOR_BEGIN, ""), \
    TOKEN_KIND(C_TOKEN_EQUALS, "="), \
    TOKEN_KIND(C_TOKEN_ADD, "+"), \
    TOKEN_KIND(C_TOKEN_SUB, "-"), \
    TOKEN_KIND(C_TOKEN_MUL, "*"), \
    TOKEN_KIND(C_TOKEN_DIV, "/"), \
    TOKEN_KIND(C_TOKEN_OR, "||"), \
    TOKEN_KIND(C_TOKEN_CMP_AND, "&&"), \
    TOKEN_KIND(C_TOKEN_CMP_NOT, "!"), \
\
TOKEN_KIND(C_TOKEN__COMPARISON_BEGIN, ""), \
    TOKEN_KIND(C_TOKEN_CMP_EQ, "=="), \
    TOKEN_KIND(C_TOKEN_CMP_NE, "!="), \
    TOKEN_KIND(C_TOKEN_CMP_LT, "<"), \
    TOKEN_KIND(C_TOKEN_CMP_GT, ">"), \
TOKEN_KIND(C_TOKEN__COMPARISON_END, ""), \
\
    TOKEN_KIND(C_TOKEN_PAREN_OPEN, "("), \
    TOKEN_KIND(C_TOKEN_PAREN_CLOSE, ")"), \
    TOKEN_KIND(C_TOKEN_COMMA, ","), \
    TOKEN_KIND(C_TOKEN_SEMICOLON, ";"), \
TOKEN_KIND(C_TOKEN__OPERATOR_END, ""), \
\
TOKEN_KIND(C_TOKEN__KEYWORD_BEGIN, ""), \
    TOKEN_KIND(C_TOKEN_MAIN, "main"), \
    TOKEN_KIND(C_TOKEN_END, "end"), \
    TOKEN_KIND(C_TOKEN_IF, "if"), \
    TOKEN_KIND(C_TOKEN_ELIF, "elif"), \
    TOKEN_KIND(C_TOKEN_ELSE, "else"), \
    TOKEN_KIND(C_TOKEN_TRUE, "true"), \
    TOKEN_KIND(C_TOKEN_FALSE, "false"), \
    TOKEN_KIND(C_TOKEN_READ, "read"), \
    TOKEN_KIND(C_TOKEN_WRITE, "write"), \
    TOKEN_KIND(C_TOKEN_WRITELN, "writeln"), \
    TOKEN_KIND(C_TOKEN_REPEAT, "repeat"), \
    TOKEN_KIND(C_TOKEN_UNTIL, "until"), \
    TOKEN_KIND(C_TOKEN_WHILE, "while"), \
TOKEN_KIND(C_TOKEN__KEYWORD_END, ""), \
    TOKEN_KIND(C_TOKEN_COUNT, "")

typedef enum {
#define TOKEN_KIND(e, s) e
    TOKEN_KINDS
#undef TOKEN_KIND
} TokenKind;

const String token_strings[] = {
#define TOKEN_KIND(e, s) {(const u8*)s, sizeof(s) - 1}
    TOKEN_KINDS
#undef TOKEN_KIND
};

typedef struct {
    i32 line;
    i32 col;
} TokenPos;

typedef struct {
    TokenKind kind;
    TokenPos pos;
    String str;
} Token;

typedef struct {
    const u8 *start;
    const u8 *end;
    const u8 *cur;
    const u8 *next;
    Rune cur_rune;
    TokenPos pos;
} Tokenizer;

typedef struct {
    u8 lo, hi;
} Utf8AcceptRange;

static const Utf8AcceptRange g_utf8_accept_ranges[] = {
    {0x80, 0xBF},
    {0xA0, 0xBF},
    {0x80, 0x9F},
    {0x90, 0xBF},
    {0x80, 0x8F},
};

static isize utf8_decode(String str, Rune *rune_out)
{
    if (str.len < 1) {
        return 0;
    }

    isize width = 0;
    (void)width;
    // TODO(cya): implement
    *rune_out = *str.text;
    return 1;
}

static void tokenizer_advance_to_next_rune(Tokenizer *t)
{
    if (t->cur_rune == '\n') {
        t->pos.line += 1;
        t->pos.col = 1;
    }

    if (t->next >= t->end) {
        t->cur = t->end;
        t->cur_rune = CY_RUNE_EOF;
        return;
    }

    t->cur = t->next;
    Rune rune = *t->cur;
    if (rune == 0) {
        // TODO(cya): error invalid NUL char
        t->next += 1;
    } else if (rune & 0x80) {
        // NOTE(cya): not ASCII
        isize width = utf8_decode((String){
            .text = t->cur,
            .len = t->end - t->cur
        }, &rune);
        t->next += width;
    } else {
        t->next += 1;
    }

    t->cur_rune = rune;
    t->pos.col += 1;
}

static Tokenizer tokenizer_init(String src)
{
    const u8 *start = (const u8*)src.text;
    const u8 *end = start + src.len;
    Tokenizer t = {
        .start = start,
        .end = end,
        .cur = start,
        .next = start,
        .pos = (TokenPos){
            .line = 1,
        },
    };

    tokenizer_advance_to_next_rune(&t);
    if (t.cur_rune == CY_RUNE_BOM) { // NOTE(cya): is this even necessary?
        tokenizer_advance_to_next_rune(&t);
    }

    return t;
}

static inline b32 rune_is_letter(Rune r)
{
    if (r < 0x80) {
        // NOTE(cya): (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z');
        return ((u32)r | 0x20) - 0x61 < 26;
    } else {
        // TODO(cya): detect non-ascii unicode letters
        return false;
    }
}

static inline b32 rune_is_digit(Rune r)
{
    if (r < 0x80) {
        return (u32)r - '0' < 10;
    } else {
        return false;
    }
}

static inline b32 rune_is_alphanumeric(Rune r)
{
    return rune_is_letter(r) || rune_is_digit(r);
}

static inline b32 rune_is_uppercase(Rune r) {
    return (u32)r - 0x41 < 26;
}

static b32 token_is_ident(String tok)
{
    if (tok.len < 3) {
        return false;
    }
    
    const u8 *s = tok.text;
    b32 has_prefix = (s[1] == '_' && (
        s[0] == 'i' || s[0] == 'f' || s[0] == 'b' || s[0] == 's'
    ));
    if (!has_prefix) {
        return false;
    }

    for (isize i = 2; i < tok.len; i++) {
        Rune r = s[i];
        if (!rune_is_alphanumeric(r)) {
            return false;
        }
        
        b32 has_adjacent_uppercases = i < tok.len - 1 &&
            rune_is_uppercase(r) && rune_is_uppercase(s[i + 1]);
        if (has_adjacent_uppercases) {
            return false;
        }            
    }
    
    return true;
}

static b32 token_is_keyword(String tok, i32 *kind_out)
{
    for (isize i = C_TOKEN__KEYWORD_BEGIN + 1; i < C_TOKEN__KEYWORD_END; i++) {
        if (cy_string_view_are_equal(tok, token_strings[i])) {
            *kind_out = i;
            return true;
        }
    }

    return false;
}

static String string_from_token_kind(TokenKind kind)
{
    if (kind < 0 || kind >= C_TOKEN_COUNT) {
        return token_strings[C_TOKEN_INVALID];
    } else if (kind > C_TOKEN__OPERATOR_BEGIN && kind < C_TOKEN__OPERATOR_END) {
        return cy_string_view_create_c("símbolo especial");
    } else if (kind > C_TOKEN__KEYWORD_BEGIN && kind < C_TOKEN__KEYWORD_END) {
        return cy_string_view_create_c("palavra reservada");
    }

    return token_strings[kind];
}

static TokenKind token_kind_from_string(String str)
{
    for (isize i = 0; i < C_TOKEN_COUNT; i++) {
        if (cy_string_view_are_equal(str, token_strings[i])) {
            return i;
        }
    }

    return C_TOKEN_INVALID; 
}

static Token tokenizer_get_token(Tokenizer *t)
{
    TokenPos cur_pos = t->pos;
    Token token = {
        .kind = C_TOKEN_INVALID,
        .pos.line = cur_pos.line,
        .pos.col = cur_pos.col,
        .str = (String){
            .text = t->cur,
            .len = 1,
        },
    };

    Rune cur_rune = t->cur_rune;
    if (rune_is_letter(cur_rune)) {
        // NOTE(cya): could be either ident or keyword
        while (rune_is_alphanumeric(cur_rune) || cur_rune == '_') {
            tokenizer_advance_to_next_rune(t);
            cur_rune = t->cur_rune;
        }

        token.str.len = t->cur - token.str.text;

        i32 kind;
        if (token_is_ident(token.str)) {
            token.kind = C_TOKEN_IDENT;
            return token;
        } else if (token_is_keyword(token.str, &kind)) {
            token.kind = kind;
            return token;
        } else {
            // TODO(cya): malformed alphanumeric token
            return token;
        }
    } else {
        switch (cur_rune) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9': {
             // TODO(cya): numeric constant
        } break;
        case '"': {
            do {
                tokenizer_advance_to_next_rune(t);
                cur_rune = t->cur_rune;

                if (cur_rune == '%' && t->next < t->end && *t->next != 'x') {
                    // TODO(cya): invalid format specifier
                    return token;
                }
                if (cur_rune == '\n') {
                    // TODO(cya): line break inside string literal
                    return token;
                }
            } while (t->next < t->end && *t->next != '"'); 

            if (t->next >= t->end) {
                // TODO(cya): missing closing quote marks
                return token;
            } else {
                tokenizer_advance_to_next_rune(t);
            }
            
            tokenizer_advance_to_next_rune(t);
            
            token.str.len = t->cur - token.str.text;
            token.kind = C_TOKEN_STRING;
            return token;
        } break;
        case ';':
        case ',':
        case '(':
        case ')':
        case '<':
        case '+':
        case '-':
        case '*':
        case '/': {
            String s = cy_string_view_create_len((const char*)t->cur, 1);
            tokenizer_advance_to_next_rune(t);
            token.kind = token_kind_from_string(s);
            return token;
        } break;
        case '&': 
        case '|': 
        case '=': 
        case '!': 
        case '>': {
            String s = cy_string_view_create_len((const char*)t->cur, 1);
            if (t->next < t->end) {
                Rune next_rune = *t->next;
                b32 is_cmp = (cur_rune == '&' && next_rune == '&') ||
                    (cur_rune == '|' && next_rune == '|') ||
                    (cur_rune == '=' && next_rune == '=') ||
                    (cur_rune == '!' && next_rune == '=');
                b32 is_comment = (cur_rune == '>' && next_rune == '@');
                if (is_cmp) {
                    s.len += 1;
                    tokenizer_advance_to_next_rune(t);
                } else if (is_comment) {
                    tokenizer_advance_to_next_rune(t);
                    isize diff = t->end - t->next;
                    if (diff < 6) {
                        // TODO(cya): incomplete comment literal
                    }
                    
                    if (*t->next != '\r' || *(t->next + 1) != '\n') {
                        // TODO(cya): missing newline
                    }

                    while (t->cur < t->end && t->cur_rune != '@') {
                        tokenizer_advance_to_next_rune(t);
                    }

                    if (t->end - t->cur < 2 || *t->next != '<') {
                        // TODO(cya): incomplete comment literal
                    }

                    if (*(t->cur - 1) != '\n' || *(t->cur - 2) != '\r') {
                        // TODO(cya): missing newline
                    } else {
                        tokenizer_advance_to_next_rune(t);
                    }

                    tokenizer_advance_to_next_rune(t);
                    
                    token.str.len = t->cur - token.str.text;
                    token.kind = C_TOKEN_COMMENT;
                    return token;
                }
            }

            tokenizer_advance_to_next_rune(t);
            token.kind = token_kind_from_string(s);
            return token;
        } break;
        default: {
            // TODO(cya): either whitespace(\s\t\r\n) or invalid symbol
        }
        }
    }

    token.str.len = t->cur - token.str.text;
    return token;
}

typedef struct {
    Token *arr;
    isize len;
    isize cap;
} TokenList;

#define C_TOKEN_LIST_INIT_CAP 0x1000

static TokenList tokenize(CyAllocator a, String src)
{
    TokenList list = {
        .cap = C_TOKEN_LIST_INIT_CAP,
    };

    list.arr = cy_alloc_array(a, Token, list.cap);
    if (list.arr == NULL) {
        // TODO(cya): error handling
    }

    Tokenizer t = tokenizer_init(src);
    while (t.cur_rune != CY_RUNE_EOF) {
        if (list.len == list.cap) {
            isize new_cap = list.cap * 2;
            list.arr = cy_resize_array(a, list.arr, Token, list.cap, new_cap);
            if (list.arr == NULL) {
                // TODO(cya): error handling
            }

            list.cap = new_cap;
        }

        *list.arr = tokenizer_get_token(&t);
        list.len += 1;
    }

    return list;
}

static int int_to_utf8(isize n, isize max_digits, char *buf, isize cap)
{
    isize dividend = 10;
    isize digits = 1;
    while (n % dividend != n) {
        dividend *= 10;
        digits += 1;
    }

    while (digits > max_digits) {
        dividend /= 10;
        n %= dividend;
        digits -= 1;
    }

    for (isize i = 0; i < digits && i < cap; i++) {
        dividend /= 10;
        buf[i] = '0' + n / dividend;
        n %= dividend;
    }

    buf[cap] = '\0';
    return digits;
}

static CyString append_token_info(
    CyString str,
    String line,
    String kind, String token
) {
    str = cy_string_append_view(str, line);
    str = cy_string_pad_right(str, 10, ' ');

    // TODO(cya): replace with utf8_width
    isize col_width = cy_string_len(str) + 24;
    str = cy_string_append_view(str, kind);
    str = cy_string_pad_right(str, col_width, ' ');

    str = cy_string_append_view(str, token);
    str = cy_string_append_c(str, "\r\n");

    return str;
}

#define LINE_NUM_MAX_DIGITS 9

static CyString append_tokens_fmt(CyString str, const TokenList *l)
{
    str = append_token_info(
        str,
        cy_string_view_create_c("linha"),
        cy_string_view_create_c("classe"),
        cy_string_view_create_c("lexema")
    );
    for (isize i = 0; i < l->len; i++) {
        Token *t = &l->arr[i];

        char line_buf[LINE_NUM_MAX_DIGITS + 1];
        isize line_buf_len = CY_STATIC_STR_LEN(line_buf);
        int_to_utf8(t->pos.line, line_buf_len, line_buf, line_buf_len);

        String line = cy_string_view_create_len(line_buf, line_buf_len);
        String token_kind = string_from_token_kind(t->kind);
        String token = t->str;

        str = append_token_info(str, line, token_kind, token);
    }

    return str;
}

CyString compile(String src_code)
{
    TokenList token_list = tokenize(cy_heap_allocator(), src_code);
    // TODO(cya): error handling

    CyString output = cy_string_create_reserve(cy_heap_allocator(), 0x1000);
    output = append_tokens_fmt(output, &token_list);

    cy_free(cy_heap_allocator(), token_list.arr);

    return output;
}
