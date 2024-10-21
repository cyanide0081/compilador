#define CY_IMPLEMENTATION
#include "cy.h"

typedef CyStringView String;

/* ---------------------------- Tokenizer ----------------------------------- */
#define TOKEN_KINDS \
    TOKEN_KIND(C_TOKEN_INVALID, "símbolo inválido"), \
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
    TOKEN_KIND(C_TOKEN_AND, "&&"), \
    TOKEN_KIND(C_TOKEN_NOT, "!"), \
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

#define KEYWORD_COUNT (C_TOKEN__KEYWORD_END - C_TOKEN__KEYWORD_BEGIN - 1)

typedef enum {
#define TOKEN_KIND(e, s) e
    TOKEN_KINDS
#undef TOKEN_KIND
} TokenKind;

const String g_token_strings[] = {
#define TOKEN_KIND(e, s) {(const u8*)s, CY_STATIC_STR_LEN(s)}
    TOKEN_KINDS
#undef TOKEN_KIND
};

static isize g_keyword_map_arr[KEYWORD_COUNT * 3];

typedef struct {
    i32 line;
    i32 col;
} TokenPos;

typedef struct {
    TokenKind kind;
    TokenPos pos;
    String str;
} Token;

typedef enum {
    T_ERR_OUT_OF_MEMORY = -1,
    T_ERR_NONE,
    T_ERR_INVALID_SYMBOL,
    T_ERR_INVALID_KEYWORD,
    T_ERR_INVALID_IDENT,
    T_ERR_INVALID_STRING,
    T_ERR_INVALID_COMMENT,
} TokenizerError;

typedef struct {
    const u8 *start;
    const u8 *end;
    const u8 *cur;
    const u8 *next;

    Rune cur_rune;
    TokenPos pos;

    TokenizerError err;
    const u8 *err_desc;
    Token bad_tok;
} Tokenizer;

typedef struct {
    u8 lo, hi;
} Utf8AcceptRange;

typedef isize (*HashFunc)(String);

typedef struct {
    isize *data;
    isize len;
    HashFunc hash_func;
} KeywordMap;

static KeywordMap g_keyword_map;

static KeywordMap keyword_map_init(
    isize *arr, isize len, HashFunc hash_func
) {
    return (KeywordMap){
        .data = (void*)arr,
        .len = len,
        .hash_func = hash_func,
    };
}

void keyword_map_insert(KeywordMap *map, String key, isize val)
{
    if (map != NULL) {
        isize idx = map->hash_func(key);
        if (idx == -1)  {
            return;
        }

        idx %= map->len;
        CY_ASSERT_MSG(
            map->data[idx] == 0,
            "hash collision detected (idx: %td, cur val: %td, new val: %td)",
            idx, map->data[idx], val
        );

        map->data[idx] = val;
    }
}

isize keyword_hash_func(String key)
{
    isize len = key.len;
    if (len < 2) {
        return -1;
    }

    // NOTE(cya): uniqueness <- 1st char, last char and length
    u16 l = (u16)key.text[0] << 8, r = key.text[len - 2];
    return (isize)((l | r) * key.len);
}

static isize keyword_map_lookup(KeywordMap *map, String key)
{
    isize idx = map->hash_func(key);
    if (idx == -1) {
        return -1;
    }

    idx %= map->len;
    return map->data[idx];
}

#if 0
// TODO(cya): implement this business (prob inside cy.h)
static const Utf8AcceptRange g_utf8_accept_ranges[] = {
    {0x80, 0xBF},
    {0xA0, 0xBF},
    {0x80, 0x9F},
    {0x90, 0xBF},
    {0x80, 0x8F},
};
#endif

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
        // TODO(cya): test this
        t->cur_rune = CY_RUNE_INVALID;
        return;
    } else if (rune & 0x80) {
        // NOTE(cya): non-ASCII UTF-8 code unit
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

static inline Tokenizer tokenizer_init(String src)
{
    if (g_keyword_map.data == NULL) {
        g_keyword_map = keyword_map_init(
            g_keyword_map_arr, CY_STATIC_ARR_LEN(g_keyword_map_arr),
            keyword_hash_func
        );

        isize offset = C_TOKEN__KEYWORD_BEGIN + 1;
        for (isize i = 0; i < KEYWORD_COUNT; i++) {
            isize idx = offset + i;
            keyword_map_insert(&g_keyword_map, g_token_strings[idx], idx);
        }
    }

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

static inline b32 token_is_ident(String tok)
{
    if (tok.len < 3) {
        return false;
    }

    const u8 *s = tok.text;
    b32 has_prefix = (s[1] == '_' && (
        s[0] == 'i' || s[0] == 'f' || s[0] == 'b' || s[0] == 's'
    ));
    if (!has_prefix || !rune_is_letter(s[2])) {
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

static inline b32 token_is_keyword(String tok, i32 *kind_out)
{
    isize idx = keyword_map_lookup(&g_keyword_map, tok);
    if (idx == -1) {
        return false;
    }

    String keyword = g_token_strings[idx];
    if (*tok.text != *keyword.text || !cy_string_view_are_equal(tok, keyword)) {
        return false;
    }

    *kind_out = idx;
    return true;
}

static inline b32 is_whitespace(Rune r)
{
    // TODO(cya): maybe detect Unicode whitespace chars?
    return r == ' ' || r == '\t' ||
        r == '\r' || r == '\n' ||
        r == '\v' || r == '\f';
}

static inline void tokenizer_skip_whitespace(Tokenizer *t)
{
    while (is_whitespace(t->cur_rune)) {
        tokenizer_advance_to_next_rune(t);
    }
}

static inline String string_from_token_kind(TokenKind kind)
{
    if (kind < 0 || kind >= C_TOKEN_COUNT) {
        return g_token_strings[C_TOKEN_INVALID];
    } else if (kind > C_TOKEN__OPERATOR_BEGIN && kind < C_TOKEN__OPERATOR_END) {
        return cy_string_view_create_c("símbolo especial");
    } else if (kind > C_TOKEN__KEYWORD_BEGIN && kind < C_TOKEN__KEYWORD_END) {
        return cy_string_view_create_c("palavra reservada");
    }

    return g_token_strings[kind];
}

static inline CyString cy_string_from_token_kind(CyAllocator a, TokenKind kind)
{
    return cy_string_create_view(a, string_from_token_kind(kind));
}

static inline TokenKind token_kind_from_string(String str)
{
    for (isize i = 0; i < C_TOKEN_COUNT; i++) {
        if (cy_string_view_are_equal(str, g_token_strings[i])) {
            return i;
        }
    }

    return C_TOKEN_INVALID;
}

static inline void tokenizer_error(
    Tokenizer *t, Token *bad_tok, TokenizerError err, const char *err_desc
) {
    if (bad_tok != NULL) {
        t->bad_tok = *bad_tok;
    }

    t->err = err;
    t->err_desc = (const u8*)err_desc;
}

static inline void tokenizer_parse_numerical_constant(
    Tokenizer *t, Token *token_out
) {
    if (t->cur_rune != '0') {
        do {
            tokenizer_advance_to_next_rune(t);
        } while ((rune_is_digit(t->cur_rune)));
    } else {
        tokenizer_advance_to_next_rune(t);
        t->cur_rune = t->cur_rune;
    }

    b32 is_float = t->cur_rune == ',' &&
        t->next < t->end && rune_is_digit(*t->next);
    if (is_float) {
        token_out->kind = C_TOKEN_FLOAT;

        tokenizer_advance_to_next_rune(t);
        t->cur_rune = t->cur_rune;

        const u8 *start = t->cur;
        const u8 *end = start;
        do {
            end += 1;
        } while ((rune_is_digit(*end)));

        while (*(end - 1) == '0') {
            end -= 1;
        }

        isize adv_len = end - start;
        if (end == start && *start == '0') {
            adv_len = 1;
        }

        while (adv_len-- > 0) {
            tokenizer_advance_to_next_rune(t);
            t->cur_rune = t->cur_rune;
        }
    } else {
        token_out->kind = C_TOKEN_INTEGER;
    }
}

static inline void tokenizer_parse_string_literal(
    Tokenizer *t, Token *token_out
) {
    do {
        tokenizer_advance_to_next_rune(t);

        if (t->cur_rune == '%' && t->next < t->end && *t->next != 'x') {
            const char *s = (const char*)t->cur;
            Token bad_tok = {
                .kind = C_TOKEN_INVALID,
                .pos = token_out->pos,
                .str = cy_string_view_create_len(s, 1),
            };
            if (*t->next != '"') {
                bad_tok.str.len += 1;
            }

            // TODO(cya): indicate col pos of bad tok when we switch to arenas
            tokenizer_error(
                t, &bad_tok, T_ERR_INVALID_STRING,
                "especificador de formato inválido (%*)"
            );
            return;
        } else if (t->cur_rune == '\n') {
            tokenizer_error(
                t, token_out, T_ERR_INVALID_STRING,
                "quebra de linha ilegal dentro da literal"
            );
            return;
        }
    } while (t->cur < t->end && *t->cur != '"');

    if (t->cur >= t->end) {
        tokenizer_error(
            t, token_out, T_ERR_INVALID_STRING, "feche-a com \""
        );
        return;
    }

    tokenizer_advance_to_next_rune(t);
    token_out->kind = C_TOKEN_STRING;
}

static inline void tokenizer_parse_comment(Tokenizer *t, Token *token_out)
{
    const u8 *start = t->cur;
    token_out->str.len += 1;

    tokenizer_advance_to_next_rune(t);
    tokenizer_advance_to_next_rune(t);

    while (t->cur < t->end && t->cur_rune != '@') {
        tokenizer_advance_to_next_rune(t);
    }

    if (t->end - t->cur < 2) {
        tokenizer_error(
            t, token_out, T_ERR_INVALID_COMMENT,
            "feche-o com uma quebra de linha seguida por @<"
        );
        return;
    } else if (*t->next != '<') {
        tokenizer_error(
            t, token_out, T_ERR_INVALID_COMMENT, "@ ilegal dentro da literal"
        );
        return;
    }

    const u8 *end = t->next;
    tokenizer_advance_to_next_rune(t);
    tokenizer_advance_to_next_rune(t);

    // NOTE(cya): should detect CR/LF/CRLF line endings correctly
    b32 malformed =
        (*(end - 2) != '\n' && *(end - 2) != '\r') ||
        (*(start + 2) != '\r' && *(start + 2) != '\n');
    if (malformed) {
        tokenizer_error(t, token_out, T_ERR_INVALID_COMMENT, "mal formatado");
        return;
    }

    token_out->kind = C_TOKEN_COMMENT;
}

static Token tokenizer_get_token(Tokenizer *t)
{
    tokenizer_skip_whitespace(t);

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
        while (rune_is_alphanumeric(cur_rune) || cur_rune == '_') {
            tokenizer_advance_to_next_rune(t);
            cur_rune = t->cur_rune;
        }

        token.str.len = t->cur - token.str.text;
        b32 not_keyword = cy_string_view_contains(token.str, "_0123456789");
        if (not_keyword) {
            if (token_is_ident(token.str)) {
                token.kind = C_TOKEN_IDENT;
            } else {
                tokenizer_error(t, &token, T_ERR_INVALID_IDENT, NULL);
            }
        } else  {
            i32 kind;
            if (token_is_keyword(token.str, &kind)) {
                token.kind = kind;
                return token;
            } else {
                tokenizer_error(t, &token, T_ERR_INVALID_KEYWORD, NULL);
            }
        }
    } else {
        switch (cur_rune) {
        case CY_RUNE_EOF: {
            token.kind = C_TOKEN_EOF;
        } break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            tokenizer_parse_numerical_constant(t, &token);
        } break;
        case '"': {
            tokenizer_parse_string_literal(t, &token);
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
                    tokenizer_advance_to_next_rune(t);
                    token.kind = token_kind_from_string(s);
                } else if (is_comment) {
                    tokenizer_parse_comment(t, &token);
                } else {
                    tokenizer_advance_to_next_rune(t);
                    token.kind = token_kind_from_string(s);
                }
            } else {
                tokenizer_advance_to_next_rune(t);
                token.kind = token_kind_from_string(s);
            }
        } break;
        default: {
            tokenizer_error(t, &token, T_ERR_INVALID_SYMBOL, NULL);
        } break;
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

static TokenList tokenize(CyAllocator a, Tokenizer *t, b32 ignore_comments)
{
    TokenList list = {
        .cap = C_TOKEN_LIST_INIT_CAP,
    };
    list.arr = cy_alloc_array(a, Token, list.cap);
    if (list.arr == NULL) {
        tokenizer_error(t, NULL, T_ERR_OUT_OF_MEMORY, NULL);
        cy_mem_set(&list, 0, sizeof(list));
        return list;
    }

    isize e = 0;
    for (;;) {
        if (list.len == list.cap) {
            isize new_cap = list.cap * 2;
            list.arr = cy_resize_array(a, list.arr, Token, list.cap, new_cap);
            if (list.arr == NULL) {
                tokenizer_error(t, NULL, T_ERR_OUT_OF_MEMORY, NULL);
                cy_mem_set(&list, 0, sizeof(list));
                break;
            }

            list.cap = new_cap;
        }

        Token new_tok = tokenizer_get_token(t);
        if (ignore_comments && new_tok.kind == C_TOKEN_COMMENT) {
            continue;
        }

        list.arr[e++] = new_tok;
        if (t->err != T_ERR_NONE) {
            cy_free(a, list.arr);
            cy_mem_set(&list, 0, sizeof(list));
            break;
        }

        list.len += 1;
        if (new_tok.kind == C_TOKEN_EOF) {
            break;
        }
    }

    return list;
}

#if 0
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
    CyString str, String line,
    String kind, String token
) {
    CyAllocator a = CY_STRING_HEADER(str)->alloc;
    CyString new_line = cy_string_create_reserve(a, 0x20);
    new_line = cy_string_append_view(new_line, line);
    new_line = cy_string_pad_right(new_line, 10, ' ');

    // TODO(cya): replace with utf8_width
    isize col_width = cy_utf8_codepoints(new_line) + 22;
    new_line = cy_string_append_view(new_line, kind);
    new_line = cy_string_pad_right(new_line, col_width, ' ');

    new_line = cy_string_append_view(new_line, token);
    str = cy_string_append(str, new_line);

    cy_string_free(new_line);
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
        isize line_buf_cap = CY_STATIC_STR_LEN(line_buf);
        isize line_buf_len = int_to_utf8(
            t->pos.line, line_buf_cap, line_buf, line_buf_cap
        );

        String line = cy_string_view_create_len(line_buf, line_buf_len);
        String token_kind = string_from_token_kind(t->kind);
        String token = t->str;

        str = cy_string_append_c(str, "\r\n");
        str = append_token_info(str, line, token_kind, token);
    }

    return str;
}
#endif

static inline CyString append_error_prefix(CyString str, TokenPos err_pos)
{
    return cy_string_append_fmt(str, "Erro na linha %td – ", err_pos.line);
}

static CyString tokenizer_create_error_msg(CyAllocator a, const Tokenizer *t)
{
    if (t->err == T_ERR_OUT_OF_MEMORY) {
        return NULL; // NOTE(cya): since we're out of memory
    }

    CyString msg = cy_string_create_reserve(a, 0x40);
    msg = append_error_prefix(msg, t->bad_tok.pos);

    TokenizerError err = t->err;
    if (err != T_ERR_INVALID_STRING && err != T_ERR_INVALID_COMMENT) {
        msg = cy_string_append_fmt(
            msg, "%.*s ", (int)t->bad_tok.str.len, t->bad_tok.str.text
        );
    }

    const char *desc = NULL;
    switch (err) {
    case T_ERR_INVALID_SYMBOL: {
        desc = "símbolo inválido";
    } break;
    case T_ERR_INVALID_KEYWORD: {
        desc = "palavra reservada inválida";
    } break;
    case T_ERR_INVALID_IDENT: {
        desc = "identificador inválido";
    } break;
    case T_ERR_INVALID_STRING: {
        desc = "constante_string inválida";
    } break;
    case T_ERR_INVALID_COMMENT: {
        desc = "comentário inválido";
    } break;
    default: {
        desc = "(fatal) erro não reconhecido ao tokenizar código";
    } break;
    }

    msg = cy_string_append_c(msg, desc);
    if (t->err_desc != NULL) {
        msg = cy_string_append_fmt(msg, " (%s)", t->err_desc);
    }

    return msg;
}

/* ----------------------------- Parser ------------------------------------- */
#define NON_TERMINALS \
    NON_TERMINAL(NT_START, "<main>"), \
\
NON_TERMINAL(NT__INSTRUCTION_BEGIN, ""), \
    NON_TERMINAL(NT_INSTR_LIST, "<lista_instr>"), \
    NON_TERMINAL(NT_INSTR_LIST_R, "<lista_instr_rep>"), \
    NON_TERMINAL(NT_INSTRUCTION, "<instrucao>"), \
    NON_TERMINAL(NT_DEC_OR_ASSIGN, "<dec_ou_atr>"), \
    NON_TERMINAL(NT_ASSIGN_OPT, "<atr_opt>"), \
    NON_TERMINAL(NT_ID_LIST, "<lista_id>"), \
    NON_TERMINAL(NT_ID_LIST_R, "<lista_id_mul>"), \
NON_TERMINAL(NT__INSTRUCTION_END, ""), \
\
NON_TERMINAL(NT__COMMAND_BEGIN, ""), \
    NON_TERMINAL(NT_CMD, "<cmd>"), \
    NON_TERMINAL(NT_CMD_ASSIGN, "<cmd_atr>"), \
    NON_TERMINAL(NT_CMD_INPUT, "<cmd_entr>"), \
    NON_TERMINAL(NT_INPUT_LIST, "<lista_entr>"), \
    NON_TERMINAL(NT_INPUT_LIST_R, "<lista_entr_mul>"), \
    NON_TERMINAL(NT_STRING_OPT, "<cte_str_opt>"), \
    NON_TERMINAL(NT_CMD_OUTPUT, "<cmd_saida>"), \
    NON_TERMINAL(NT_CMD_OUTPUT_KEYWORD, "<cmd_saida_tipo>"), \
    NON_TERMINAL(NT_CMD_COND, "<cmd_sel>"), \
    NON_TERMINAL(NT_ELIF, "<elif>"), \
    NON_TERMINAL(NT_ELSE, "<else>"), \
    NON_TERMINAL(NT_CMD_LIST, "<lista_cmd>"), \
    NON_TERMINAL(NT_CMD_LIST_R, "<lista_cmd_mul>"), \
    NON_TERMINAL(NT_CMD_LOOP, "<cmd_rep>"), \
    NON_TERMINAL(NT_CMD_LOOP_KEYWORD, "<cmd_rep_tipo>"), \
NON_TERMINAL(NT__COMMAND_END, ""), \
\
NON_TERMINAL(NT__EXPRESSION_BEGIN, ""), \
    NON_TERMINAL(NT_EXPR_LIST, "<lista_expr>"), \
    NON_TERMINAL(NT_EXPR_LIST_R, "<lista_expr_mul>"), \
    NON_TERMINAL(NT_EXPR, "<expr>"), \
    NON_TERMINAL(NT_EXPR_LOG, "<expr_log>"), \
    NON_TERMINAL(NT_ELEMENT, "<elemento>"), \
    NON_TERMINAL(NT_RELATIONAL, "<relacional>"), \
    NON_TERMINAL(NT_RELATIONAL_R, "<relacional_mul>"), \
    NON_TERMINAL(NT_RELATIONAL_OP, "<operador_relacional>"), \
    NON_TERMINAL(NT_ARITHMETIC, "<aritmetica>"), \
    NON_TERMINAL(NT_ARITHMETIC_R, "<aritmetica_mul>"), \
    NON_TERMINAL(NT_TERM, "<termo>"), \
    NON_TERMINAL(NT_TERM_R, "<termo_mul>"), \
    NON_TERMINAL(NT_FACTOR, "<fator>"), \
NON_TERMINAL(NT__EXPRESSION_END, ""), \
    NON_TERMINAL(NT_COUNT, ""),

#define NT_IS_OF_CLASS(n, c) (n > NT__##c##_BEGIN && n < NT__##c##_END)

typedef enum {
#define NON_TERMINAL(e, s) e
    NON_TERMINALS
#undef NON_TERMINAL
} NonTerminal;

const String g_non_terminal_strings[] = {
#define NON_TERMINAL(e, s) {(const u8*)s, CY_STATIC_STR_LEN(s)}
    NON_TERMINALS
#undef NON_TERMINAL
};

static inline String non_terminal_name(NonTerminal n)
{
    if (n < 0 || n >= NT_COUNT) {
        return cy_string_view_create_c("<ERRO>");
    }

    return g_non_terminal_strings[n];
}

static inline CyString reachable_terminals(CyAllocator a, NonTerminal n);

static inline CyString non_terminal_description(CyAllocator a, NonTerminal n)
{
    return (NT_IS_OF_CLASS(n, EXPRESSION)) ?
        cy_string_create(a, "expressão") : reachable_terminals(a, n);
}

typedef enum {
    GR_NONE, GR_0, GR_1, GR_2, GR_3, GR_4, GR_5, GR_6, GR_7, GR_8, GR_9, GR_10,
    GR_11, GR_12, GR_13, GR_14, GR_15, GR_16, GR_17, GR_18, GR_19, GR_20, GR_21,
    GR_22, GR_23, GR_24, GR_25, GR_26, GR_27, GR_28, GR_29, GR_30, GR_31, GR_32,
    GR_33, GR_34, GR_35, GR_36, GR_37, GR_38, GR_39, GR_40, GR_41, GR_42, GR_43,
    GR_44, GR_45, GR_46, GR_47, GR_48, GR_49, GR_50, GR_51, GR_52, GR_53, GR_54,
    GR_55, GR_56, GR_57, GR_58, GR_59, GR_60, GR_61, GR_62, GR_63, GR_64, GR_65,
    GR_66, GR_67, GR_68, GR_69, GR_70, GR_71, GR_72, GR_73,
    GR_COUNT,
} GrammarRule;

CY_STATIC_ASSERT(GR_COUNT == 74 + 1);

#define RULE_IS_INVALID(r) (r <= GR_NONE || r >= GR_COUNT)

#define LL1_ROWS \
    LL1_ROW(NT_START, 0), \
    LL1_ROW(NT_INSTR_LIST, 1), \
    LL1_ROW(NT_INSTR_LIST_R, 2), \
    LL1_ROW(NT_INSTRUCTION, 3), \
    LL1_ROW(NT_DEC_OR_ASSIGN, 4), \
    LL1_ROW(NT_ASSIGN_OPT, 5), \
    LL1_ROW(NT_ID_LIST, 6), \
    LL1_ROW(NT_ID_LIST_R, 7), \
    LL1_ROW(NT_CMD, 8), \
    LL1_ROW(NT_CMD_ASSIGN, 9), \
    LL1_ROW(NT_CMD_INPUT, 10), \
    LL1_ROW(NT_INPUT_LIST, 11), \
    LL1_ROW(NT_INPUT_LIST_R, 12), \
    LL1_ROW(NT_STRING_OPT, 13), \
    LL1_ROW(NT_CMD_OUTPUT, 14), \
    LL1_ROW(NT_CMD_OUTPUT_KEYWORD, 15), \
    LL1_ROW(NT_EXPR_LIST, 16), \
    LL1_ROW(NT_EXPR_LIST_R, 17), \
    LL1_ROW(NT_CMD_COND, 18), \
    LL1_ROW(NT_ELIF, 19), \
    LL1_ROW(NT_ELSE, 20), \
    LL1_ROW(NT_CMD_LIST, 21), \
    LL1_ROW(NT_CMD_LIST_R, 22), \
    LL1_ROW(NT_CMD_LOOP, 23), \
    LL1_ROW(NT_CMD_LOOP_KEYWORD, 24), \
    LL1_ROW(NT_EXPR, 25), \
    LL1_ROW(NT_EXPR_LOG, 26), \
    LL1_ROW(NT_ELEMENT, 27), \
    LL1_ROW(NT_RELATIONAL, 28), \
    LL1_ROW(NT_RELATIONAL_R, 29), \
    LL1_ROW(NT_RELATIONAL_OP, 30), \
    LL1_ROW(NT_ARITHMETIC, 31), \
    LL1_ROW(NT_ARITHMETIC_R, 32), \
    LL1_ROW(NT_TERM, 33), \
    LL1_ROW(NT_TERM_R, 34), \
    LL1_ROW(NT_FACTOR, 35), \

#define LL1_ROW_COUNT 36

static u8 g_ll1_row_from_kind[NT_COUNT] = {
#define LL1_ROW(n, r) [n] = r
    LL1_ROWS
#undef LL1_ROW
};

#define LL1_COLS \
    LL1_COL(C_TOKEN_IDENT, 0), \
    LL1_COL(C_TOKEN_INTEGER, 1), \
    LL1_COL(C_TOKEN_FLOAT, 2), \
    LL1_COL(C_TOKEN_STRING, 3), \
    LL1_COL(C_TOKEN_SEMICOLON, 4), \
    LL1_COL(C_TOKEN_COMMA, 5), \
    LL1_COL(C_TOKEN_PAREN_OPEN, 6), \
    LL1_COL(C_TOKEN_PAREN_CLOSE, 7), \
    LL1_COL(C_TOKEN_EQUALS, 8), \
    LL1_COL(C_TOKEN_AND, 9), \
    LL1_COL(C_TOKEN_OR, 10), \
    LL1_COL(C_TOKEN_NOT, 11), \
    LL1_COL(C_TOKEN_CMP_EQ, 12), \
    LL1_COL(C_TOKEN_CMP_NE, 13), \
    LL1_COL(C_TOKEN_CMP_LT, 14), \
    LL1_COL(C_TOKEN_CMP_GT, 15), \
    LL1_COL(C_TOKEN_ADD, 16), \
    LL1_COL(C_TOKEN_SUB, 17), \
    LL1_COL(C_TOKEN_MUL, 18), \
    LL1_COL(C_TOKEN_DIV, 19), \
    LL1_COL(C_TOKEN_MAIN, 20), \
    LL1_COL(C_TOKEN_READ, 21), \
    LL1_COL(C_TOKEN_TRUE, 22), \
    LL1_COL(C_TOKEN_FALSE, 23), \
    LL1_COL(C_TOKEN_WRITE, 24), \
    LL1_COL(C_TOKEN_WRITELN, 25), \
    LL1_COL(C_TOKEN_IF, 26), \
    LL1_COL(C_TOKEN_ELIF, 27), \
    LL1_COL(C_TOKEN_ELSE, 28), \
    LL1_COL(C_TOKEN_END, 29), \
    LL1_COL(C_TOKEN_REPEAT, 30), \
    LL1_COL(C_TOKEN_WHILE, 31), \
    LL1_COL(C_TOKEN_UNTIL, 32), \
    LL1_COL(C_TOKEN_EOF, 33), \

#define LL1_COL_COUNT 34

static u8 g_ll1_col_from_kind[] = {
#define LL1_COL(k, c) [k] = c
    LL1_COLS
#undef LL1_COL
};

static u8 g_ll1_kind_from_col[] = {
#define LL1_COL(k, c) [c] = k
    LL1_COLS
#undef LL1_COL
};

static GrammarRule g_ll1_table[LL1_ROW_COUNT][LL1_COL_COUNT] = {
    { [20] = GR_0, },
    {
        [0] = GR_1, [21] = GR_1, [24] = GR_1, [25] = GR_1, [26] = GR_1,
        [30] = GR_1,
    },
    {
        [0] = GR_2, [21] = GR_2, [24] = GR_2, [25] = GR_2, [26] = GR_2,
        [29] = GR_3, [30] = GR_2,
    },
    {
        [0] = GR_4, [21] = GR_5, [24] = GR_6, [25] = GR_6, [26] = GR_8,
        [30] = GR_7,
    },
    { [0] = GR_9, },
    { [4] = GR_11, [8] = GR_10, },
    { [0] = GR_12, },
    { [4] = GR_14, [5] = GR_13, [8] = GR_14, },
    {
        [0] = GR_15, [21] = GR_16, [24] = GR_17, [25] = GR_17, [26] = GR_19,
        [30] = GR_18,
    },
    { [0] = GR_20, },
    { [21] = GR_21, },
    { [0] = GR_22, [3] = GR_22, },
    { [5] = GR_23, [7] = GR_24, },
    { [0] = GR_26, [3] = GR_25, },
    { [24] = GR_27, [25] = GR_27, },
    { [24] = GR_28, [25] = GR_29, },
    {
        [0] = GR_30, [1] = GR_30, [2] = GR_30, [3] = GR_30, [6] = GR_30,
        [11] = GR_30, [16] = GR_30, [17] = GR_30, [22] = GR_30, [23] = GR_30,
    },
    { [5] = GR_31, [7] = GR_32, },
    { [26] = GR_33, },
    { [27] = GR_34, [28] = GR_35, [29] = GR_35, },
    { [28] = GR_36, [29] = GR_37, },
    {
        [0] = GR_38, [21] = GR_38, [24] = GR_38, [25] = GR_38, [26] = GR_38,
        [30] = GR_38,
    },
    {
        [0] = GR_39, [21] = GR_39, [24] = GR_39, [25] = GR_39, [26] = GR_39,
        [27] = GR_40, [28] = GR_40, [29] = GR_40, [30] = GR_39, [31] = GR_40,
        [32] = GR_40,
    },
    { [30] = GR_41, },
    { [31] = GR_42, [32] = GR_43, },
    {
        [0] = GR_44, [1] = GR_44, [2] = GR_44, [3] = GR_44, [6] = GR_44,
        [11] = GR_44, [16] = GR_44, [17] = GR_44, [22] = GR_44,  [23] = GR_44,
    },
    {
        [0] = GR_47, [4] = GR_47, [5] = GR_47, [7] = GR_47, [9] = GR_45,
        [10] = GR_46, [21] = GR_47, [24] = GR_47, [25] = GR_47, [26] = GR_47,
        [30] = GR_47,
    },
    {
        [0] = GR_48, [1] = GR_48, [2] = GR_48, [3] = GR_48, [6] = GR_48,
        [11] = GR_51, [16] = GR_48, [17] = GR_48, [22] = GR_49, [23] = GR_50,
    },
    {
        [0] = GR_52, [1] = GR_52, [2] = GR_52, [3] = GR_52, [6] = GR_52,
        [16] = GR_52, [17] = GR_52,
    },
    {
        [0] = GR_54, [4] = GR_54, [5] = GR_54, [7] = GR_54, [9] = GR_54,
        [10] = GR_54, [12] = GR_53, [13] = GR_53, [14] = GR_53, [15] = GR_53,
        [21] = GR_54, [24] = GR_54, [25] = GR_54, [26] = GR_54, [30] = GR_54,
    },
    { [12] = GR_55, [13] = GR_56, [14] = GR_57, [15] = GR_58, },
    {
        [0] = GR_59, [1] = GR_59, [2] = GR_59, [3] = GR_59, [6] = GR_59,
        [16] = GR_59, [17] = GR_59,
    },
    {
        [0] = GR_62, [4] = GR_62, [5] = GR_62, [7] = GR_62, [9] = GR_62,
        [10] = GR_62, [12] = GR_62, [13] = GR_62, [14] = GR_62, [15] = GR_62,
        [16] = GR_60, [17] = GR_61, [21] = GR_62, [24] = GR_62, [25] = GR_62,
        [26] = GR_62, [30] = GR_62,
    },
    {
        [0] = GR_63, [1] = GR_63, [2] = GR_63, [3] = GR_63, [6] = GR_63,
        [16] = GR_63, [17] = GR_63,
    },
    {
        [0] = GR_66, [4] = GR_66, [5] = GR_66, [7] = GR_66, [9] = GR_66,
        [10] = GR_66, [12] = GR_66, [13] = GR_66, [14] = GR_66, [15] = GR_66,
        [16] = GR_66, [17] = GR_66, [18] = GR_64, [19] = GR_65, [21] = GR_66,
        [24] = GR_66, [25] = GR_66, [26] = GR_66, [30] = GR_66,
    },
    {
        [0] = GR_67, [1] = GR_68, [2] = GR_69, [3] = GR_70, [6] = GR_71,
        [16] = GR_72, [17] = GR_73,
    },
};

typedef enum {
    P_ERR_OUT_OF_MEMORY = -1,
    P_ERR_NONE,
    P_ERR_REACHED_EOF,
    P_ERR_UNEXPECTED_TOKEN,
    P_ERR_INVALID_RULE,
} ParserErrorKind;


typedef struct {
    enum {
        PARSER_KIND_TOKEN,
        PARSER_KIND_NON_TERMINAL,
    } kind;
    union {
        Token token;
        NonTerminal non_terminal;
    } u;
} ParserSymbol;

typedef struct {
    CyAllocator alloc;
    ParserSymbol *items;
    isize len;
    isize cap;
} ParserStack;

#define AST_KINDS \
    AST_KIND(IDENT, Ident, struct { \
        Token tok; \
    }) \
    AST_KIND(LITERAL, Literal, struct { \
        Token tok; \
    }) \
    AST_KIND(MAIN, Main, struct { \
        AstNode *body; \
    }) \
    AST_KIND(IDENT_LIST, IdentList, struct { \
        Token *idents; \
        isize len; \
    }) \
    AST_KIND(VAR_DECL, VarDecl, struct { \
        AstNode *ident_list; \
    }) \
    AST_KIND(ASSIGN_STMT, AssignStmt, struct { \
        AstNode *ident_list; \
        AstNode *expr; \
    }) \
    AST_KIND(READ_STMT, ReadStmt, struct { \
        AstNode *args; \
    }) \
    AST_KIND(INPUT_LIST, InputList, struct { \
        AstNode *string_opt; \
        Token ident; \
        AstNode *next; \
    }) \
    AST_KIND(STRING_OPT, StringOpt, struct { \
        Token string; \
    }) \
    AST_KIND(WRITE_STMT, WriteStmt, struct { \
        Token keyword; \
        AstNode *args; \
    }) \
    AST_KIND(EXPR_LIST, ExprList, struct { \
        AstNode *exprs; \
        isize len; \
    }) \
    AST_KIND(IF_STMT, IfStmt, struct { \
        AstNode *cond; \
        AstNode *body; \
        AstNode *else_stmt; \
    }) \
    AST_KIND(STMT_LIST, StmtList, struct { \
        AstNode **stmts; \
        isize len; \
    }) \
    AST_KIND(REPEAT_STMT, RepeatStmt, struct { \
        AstNode *body; \
        Token label; \
        AstNode *expr; \
    }) \
    AST_KIND(BINARY_EXPR, BinaryExpr, struct { \
        Token op; \
        AstNode *left; \
        AstNode *right; \
    }) \
    AST_KIND(UNARY_EXPR, UnaryExpr, struct { \
        Token op; \
        AstNode *expr; \
    }) \
    AST_KIND(PAREN_EXPR, ParenExpr, struct { \
        Token open; \
        AstNode *expr; \
        Token close; \
    }) \
    AST_KIND(KIND_COUNT, KindCount, isize)

typedef enum {
#define AST_KIND(e, ...) AST_##e,
    AST_KINDS
#undef AST_KIND
} AstKind;

typedef struct AstNode AstNode;
#define AST_KIND(e, t, s) typedef s Ast##t;
    AST_KINDS
#undef AST_KIND

struct AstNode {
    AstKind kind;
    union {
#define AST_KIND(e, t, ...) Ast##t t;
        AST_KINDS
#undef AST_KIND
    } u;
};

#define AST_NODE_ALLOC(alloc, _kind) cy_alloc(alloc, (sizeof(AstNode)))
#define AST_NODE_ALLOC_ITEM(alloc, node, _kind) cy_resize( \
    alloc, node, node->u._kind.len++ * sizeof(*node), \
    node->u._kind.len * sizeof(*node) \
)

typedef struct {
    CyAllocator alloc;
    AstNode *root;
} Ast;

typedef struct {
    ParserErrorKind kind;
    ParserSymbol expected;
    Token found;
} ParserError;

typedef struct {
    Token *read_tok;
    ParserStack stack;
    Ast *ast;
    AstNode *ast_cur;
    ParserError err;
} Parser;

#define PARSER_STACK_ALIGN (sizeof(Token))

static inline void parser_error(Parser *p, ParserErrorKind kind);

// TODO(cya): we could probably just use the stack allocator for this
static inline void parser_stack_push(Parser *p, ParserSymbol item)
{
    if (p->stack.len == p->stack.cap) {
        isize old_size = p->stack.cap * sizeof(*p->stack.items);
        isize new_size = old_size * 2;
        ParserSymbol *items = cy_default_resize_align(
            p->stack.alloc, p->stack.items,
            old_size, new_size,
            PARSER_STACK_ALIGN
        );
        if (items == NULL) {
            parser_error(p, P_ERR_OUT_OF_MEMORY);
            return;
        }

        p->stack.items = items;
        p->stack.cap *= 2;
    }

    p->stack.items[p->stack.len++] = item;
}

static inline void parser_stack_push_token(Parser *p, TokenKind kind)
{
    parser_stack_push(p, (ParserSymbol){
        .kind = PARSER_KIND_TOKEN,
        .u.token = (Token){.kind = kind, .str = g_token_strings[kind]},
    });
}

static inline void parser_stack_push_non_terminal(Parser *p, NonTerminal n)
{
    parser_stack_push(p, (ParserSymbol){
        .kind = PARSER_KIND_NON_TERMINAL,
        .u.non_terminal = n,
    });
}

static inline void parser_stack_pop(Parser *p)
{
    if (p->stack.len <= 0) {
        return;
    }

    ParserSymbol *top = &p->stack.items[--p->stack.len];
    cy_mem_set(top, 0, sizeof(*top));
}

static inline ParserSymbol *parser_stack_peek(Parser *p)
{
    CY_VALIDATE_PTR(p);
    return (p->stack.len > 0) ? &p->stack.items[p->stack.len - 1] : NULL;
}

static inline void parser_error(Parser *p, ParserErrorKind kind)
{
    if (p == NULL) {
        return;
    }

    p->err = (ParserError){
        .kind = kind,
        .expected = *(parser_stack_peek(p)),
        .found = *p->read_tok,
    };
}

static CyString reachable_terminals(CyAllocator a, NonTerminal n)
{
    CyString str = cy_string_create_reserve(a, 0x20);
    u8 table_row = g_ll1_row_from_kind[n];
    GrammarRule *ll1_row = g_ll1_table[table_row];
    for (isize i = 0; i < LL1_COL_COUNT; i++) {
        if (ll1_row[i] == GR_NONE) {
            continue;
        }

        TokenKind kind = g_ll1_kind_from_col[i];
        String s = g_token_strings[kind];
        str = cy_string_append_fmt(str, "%.*s ", s.len, s.text);
    }

    int len = cy_string_len(str);
    str[len - 1] = '\0';
    cy__string_set_len(str, len - 1);
    str = cy_string_shrink(str);

    return str;
}

static CyString parser_create_error_msg(CyAllocator a, Parser *p)
{
    CyString msg = cy_string_create_reserve(a, 0x100);
    msg = append_error_prefix(msg, p->err.found.pos);

    Token found = p->err.found;
    String found_str;
    switch (found.kind) {
    case C_TOKEN_EOF:
    case C_TOKEN_STRING: {
        found_str = g_token_strings[found.kind];
    } break;
    default: {
        found_str = found.str;
    } break;
    }

    ParserSymbol expected = p->err.expected;
    CyString expected_str = NULL;
    switch (expected.kind) {
    case PARSER_KIND_TOKEN: {
        switch (expected.u.token.kind) {
        case C_TOKEN_IDENT:
        case C_TOKEN_INTEGER:
        case C_TOKEN_FLOAT:
        case C_TOKEN_STRING: {
            expected_str = cy_string_from_token_kind(a, expected.u.token.kind);
        } break;
        default: {
            expected_str = cy_string_create_view(a, expected.u.token.str);
        } break;
        }
    } break;
    case PARSER_KIND_NON_TERMINAL: {
        expected_str = non_terminal_description(a, expected.u.non_terminal);
    } break;
    }

    msg = cy_string_append_fmt(
        msg, "encontrado %.*s esperado %s",
        found_str.len, found_str.text,
        expected_str
    );
    cy_string_free(expected_str);

    msg = cy_string_shrink(msg);
    return msg;
}

// NOTE(cya): must pass a stack allocator!
static Parser parser_init(CyAllocator stack_allocator, const TokenList *l)
{
    ParserSymbol *items = NULL;
    isize cap = CY_MAX(l->len, 0x10);
    isize size = cap * sizeof(*items);
    items = cy_alloc_align(stack_allocator, size, PARSER_STACK_ALIGN);
    if (items == NULL) {
        return (Parser){ .err.kind = P_ERR_OUT_OF_MEMORY, };
    }

    Parser p = {
        .read_tok = l->arr,
        .stack = (ParserStack){
            .alloc = stack_allocator,
            .items = items,
            .cap = cap,
        },
    };
    parser_stack_push_token(&p, C_TOKEN_EOF);
    parser_stack_push_non_terminal(&p, NT_START);

    return p;
}

static void parser_add_ast_node_from_non_terminal(Parser *p);
static void parser_add_ast_node_from_token(Parser *p);

static Ast parse(CyAllocator a, Parser *p)
{
    Ast ast = { .alloc = a };
    p->ast = &ast;

    for (;;) {
        if (p->err.kind != P_ERR_NONE) {
            break;
        } else if (p->read_tok->kind == C_TOKEN_COMMENT) {
            p->read_tok += 1;
            continue;
        }

        ParserSymbol *stack_top = parser_stack_peek(p);
        if (stack_top->kind == PARSER_KIND_TOKEN) {
            TokenKind kind = stack_top->u.token.kind;
            if (kind != p->read_tok->kind) {
                parser_error(p, P_ERR_UNEXPECTED_TOKEN);
                break;
            } else if (kind == C_TOKEN_EOF) {
                break;
            }

            parser_add_ast_node_from_token(p);
            parser_stack_pop(p);
            p->read_tok += 1;
            continue;
        }

        u8 table_row = g_ll1_row_from_kind[stack_top->u.non_terminal];
        u8 table_col = g_ll1_col_from_kind[p->read_tok->kind];
        GrammarRule rule = g_ll1_table[table_row][table_col];
        if (RULE_IS_INVALID(rule)) {
            parser_error(p, P_ERR_INVALID_RULE);
            break;
        }

        parser_add_ast_node_from_non_terminal(p);
        parser_stack_pop(p);
        switch (rule) {
        case GR_0: {  // <inicio> ::= main <lista_instr> end
            parser_stack_push_token(p, C_TOKEN_END);
            parser_stack_push_non_terminal(p, NT_INSTR_LIST);
            parser_stack_push_token(p, C_TOKEN_MAIN);
        } break;
        case GR_1: {  // <lista_instr> ::= <instrucao> ";" <lista_instr_rep>
            parser_stack_push_non_terminal(p, NT_INSTR_LIST_R);
            parser_stack_push_token(p, C_TOKEN_SEMICOLON);
            parser_stack_push_non_terminal(p, NT_INSTRUCTION);
        } break;
        case GR_2: {  // <lista_instr_rep> ::= <lista_instr>
            parser_stack_push_non_terminal(p, NT_INSTR_LIST);
        } break;
        case GR_3: {  // <lista_instr_rep> ::= î
        } break;
        case GR_4: {  // <instrucao> ::= <dec_ou_atr>
            parser_stack_push_non_terminal(p, NT_DEC_OR_ASSIGN);
        } break;
        case GR_5: {  // <instrucao> ::= <cmd_entr>
            parser_stack_push_non_terminal(p, NT_CMD_INPUT);
        } break;
        case GR_6: {  // <instrucao> ::= <cmd_saida>
            parser_stack_push_non_terminal(p, NT_CMD_OUTPUT);
        } break;
        case GR_7: {  // <instrucao> ::= <cmd_rep>
            parser_stack_push_non_terminal(p, NT_CMD_LOOP);
        } break;
        case GR_8: {  // <instrucao> ::= <cmd_sel>
            parser_stack_push_non_terminal(p, NT_CMD_COND);
        } break;
        case GR_9: {  // <dec_ou_atr> ::= <lista_id> <atr_opt>
            parser_stack_push_non_terminal(p, NT_ASSIGN_OPT);
            parser_stack_push_non_terminal(p, NT_ID_LIST);
        } break;
        case GR_10: { // <atr_opt> ::= "=" <expr>
            parser_stack_push_non_terminal(p, NT_EXPR);
            parser_stack_push_token(p, C_TOKEN_EQUALS);
        } break;
        case GR_11: { // <atr_opt> ::= î
        } break;
        case GR_12: { // <lista_id> ::= identificador <lista_id_mul>
            parser_stack_push_non_terminal(p, NT_ID_LIST_R);
            parser_stack_push_token(p, C_TOKEN_IDENT);
        } break;
        case GR_13: { // <lista_id_mul> ::= "," <lista_id>
            parser_stack_push_non_terminal(p, NT_ID_LIST);
            parser_stack_push_token(p, C_TOKEN_COMMA);
        } break;
        case GR_14: { // <lista_id_mul> ::= î
        } break;
        case GR_15: { // <cmd> ::= <cmd_atr>
            parser_stack_push_non_terminal(p, NT_CMD_ASSIGN);
        } break;
        case GR_16: { // <cmd> ::= <cmd_entr>
            parser_stack_push_non_terminal(p, NT_CMD_INPUT);
        } break;
        case GR_17: { // <cmd> ::= <cmd_saida>
            parser_stack_push_non_terminal(p, NT_CMD_OUTPUT);
        } break;
        case GR_18: { // <cmd> ::= <cmd_rep>
            parser_stack_push_non_terminal(p, NT_CMD_LOOP);
        } break;
        case GR_19: { // <cmd> ::= <cmd_sel>
            parser_stack_push_non_terminal(p, NT_CMD_COND);
        } break;
        case GR_20: { // <cmd_atr> ::= <lista_id> "=" <expr>
            parser_stack_push_non_terminal(p, NT_EXPR);
            parser_stack_push_token(p, C_TOKEN_EQUALS);
            parser_stack_push_non_terminal(p, NT_ID_LIST);
        } break;
        case GR_21: { // <cmd_entr> ::= read "(" <lista_entr> ")"
            parser_stack_push_token(p, C_TOKEN_PAREN_CLOSE);
            parser_stack_push_non_terminal(p, NT_INPUT_LIST);
            parser_stack_push_token(p, C_TOKEN_PAREN_OPEN);
            parser_stack_push_token(p, C_TOKEN_READ);
        } break;
        case GR_22: { // <lista_entr> ::= <cte_str_opt> id <lista_entr_mul>
            parser_stack_push_non_terminal(p, NT_INPUT_LIST_R);
            parser_stack_push_token(p, C_TOKEN_IDENT);
            parser_stack_push_non_terminal(p, NT_STRING_OPT);
        } break;
        case GR_23: { // <lista_entr_mul> ::= "," <lista_entr>
            parser_stack_push_non_terminal(p, NT_INPUT_LIST);
            parser_stack_push_token(p, C_TOKEN_COMMA);
        } break;
        case GR_24: { // <lista_entr_mul> ::= î
        } break;
        case GR_25: { // <cte_str_opt> ::= constante_string ","
            parser_stack_push_token(p, C_TOKEN_COMMA);
            parser_stack_push_token(p, C_TOKEN_STRING);
        } break;
        case GR_26: { // <cte_str_opt> ::= î
        } break;
        case GR_27: { // <cmd_saida> ::= <cmd_saida_tipo> "(" <lista_expr> ")"
            parser_stack_push_token(p, C_TOKEN_PAREN_CLOSE);
            parser_stack_push_non_terminal(p, NT_EXPR_LIST);
            parser_stack_push_token(p, C_TOKEN_PAREN_OPEN);
            parser_stack_push_non_terminal(p, NT_CMD_OUTPUT_KEYWORD);
        } break;
        case GR_28: { // <cmd_saida_tipo> ::= write
            parser_stack_push_token(p, C_TOKEN_WRITE);
        } break;
        case GR_29: { // <cmd_saida_tipo> ::= writeln
            parser_stack_push_token(p, C_TOKEN_WRITELN);
        } break;
        case GR_30: { // <lista_expr> ::= <expr> <lista_expr_mul>
            parser_stack_push_non_terminal(p, NT_EXPR_LIST_R);
            parser_stack_push_non_terminal(p, NT_EXPR);
        } break;
        case GR_31: { // <lista_expr_mul> ::= "," <lista_expr>
            parser_stack_push_non_terminal(p, NT_EXPR_LIST);
            parser_stack_push_token(p, C_TOKEN_COMMA);
        } break;
        case GR_32: { // <lista_expr_mul> ::= î
        } break;
        case GR_33: { // <cmd_sel> ::= if <expr> <lista_cmd> <elif> <else> end
            parser_stack_push_token(p, C_TOKEN_END);
            parser_stack_push_non_terminal(p, NT_ELSE);
            parser_stack_push_non_terminal(p, NT_ELIF);
            parser_stack_push_non_terminal(p, NT_CMD_LIST);
            parser_stack_push_non_terminal(p, NT_EXPR);
            parser_stack_push_token(p, C_TOKEN_IF);
        } break;
        case GR_34: { // <elif> ::= elif <expr> <lista_cmd> <elif>
            parser_stack_push_non_terminal(p, NT_ELIF);
            parser_stack_push_non_terminal(p, NT_CMD_LIST);
            parser_stack_push_non_terminal(p, NT_EXPR);
            parser_stack_push_token(p, C_TOKEN_ELIF);
        } break;
        case GR_35: { // <elif> ::= î
        } break;
        case GR_36: { // <else> ::= else <lista_cmd>
            parser_stack_push_non_terminal(p, NT_CMD_LIST);
            parser_stack_push_token(p, C_TOKEN_ELSE);
        } break;
        case GR_37: { // <else> ::= î
        } break;
        case GR_38: { // <lista_cmd> ::= <cmd> ";" <lista_cmd_mul>
            parser_stack_push_non_terminal(p, NT_CMD_LIST_R);
            parser_stack_push_token(p, C_TOKEN_SEMICOLON);
            parser_stack_push_non_terminal(p, NT_CMD);
        } break;
        case GR_39: { // <lista_cmd_mul> ::= <lista_cmd>
            parser_stack_push_non_terminal(p, NT_CMD_LIST);
        } break;
        case GR_40: { // <lista_cmd_mul> ::= î
        } break;
        case GR_41: { // <cmd_rep> ::= repeat <lista_cmd> <cmd_rep_tipo> <expr>
            parser_stack_push_non_terminal(p, NT_EXPR);
            parser_stack_push_non_terminal(p, NT_CMD_LOOP_KEYWORD);
            parser_stack_push_non_terminal(p, NT_CMD_LIST);
            parser_stack_push_token(p, C_TOKEN_REPEAT);
        } break;
        case GR_42: { // <cmd_rep_tipo> ::= while
            parser_stack_push_token(p, C_TOKEN_WHILE);
        } break;
        case GR_43: { // <cmd_rep_tipo> ::= until
            parser_stack_push_token(p, C_TOKEN_UNTIL);
        } break;
        case GR_44: { // <expr> ::= <elemento> <expr_log>
            parser_stack_push_non_terminal(p, NT_EXPR_LOG);
            parser_stack_push_non_terminal(p, NT_ELEMENT);
        } break;
        case GR_45: { // <expr_log> ::= "&&" <elemento> <expr_log>
            parser_stack_push_non_terminal(p, NT_EXPR_LOG);
            parser_stack_push_non_terminal(p, NT_ELEMENT);
            parser_stack_push_token(p, C_TOKEN_AND);
        } break;
        case GR_46: { // <expr_log> ::= "||" <elemento> <expr_log>
            parser_stack_push_non_terminal(p, NT_EXPR_LOG);
            parser_stack_push_non_terminal(p, NT_ELEMENT);
            parser_stack_push_token(p, C_TOKEN_OR);
        } break;
        case GR_47: { // <expr_log> ::= î
        } break;
        case GR_48: { // <elemento> ::= <relacional>
            parser_stack_push_non_terminal(p, NT_RELATIONAL);
        } break;
        case GR_49: { // <elemento> ::= true
            parser_stack_push_token(p, C_TOKEN_TRUE);
        } break;
        case GR_50: { // <elemento> ::= false
            parser_stack_push_token(p, C_TOKEN_FALSE);
        } break;
        case GR_51: { // <elemento> ::= "!" <elemento>
            parser_stack_push_non_terminal(p, NT_ELEMENT);
            parser_stack_push_token(p, C_TOKEN_NOT);
        } break;
        case GR_52: { // <relacional> ::= <aritmetica> <relacional_mul>
            parser_stack_push_non_terminal(p, NT_RELATIONAL_R);
            parser_stack_push_non_terminal(p, NT_ARITHMETIC);
        } break;
        case GR_53: { // <relacional_mul> ::= <operador_relacional> <aritmetica>
            parser_stack_push_non_terminal(p, NT_ARITHMETIC);
            parser_stack_push_non_terminal(p, NT_RELATIONAL_OP);
        } break;
        case GR_54: { // <relacional_mul> ::= î
        } break;
        case GR_55: { // <operador_relacional> ::= "=="
            parser_stack_push_token(p, C_TOKEN_CMP_EQ);
        } break;
        case GR_56: { // <operador_relacional> ::= "!="
            parser_stack_push_token(p, C_TOKEN_CMP_NE);
        } break;
        case GR_57: { // <operador_relacional> ::= "<"
            parser_stack_push_token(p, C_TOKEN_CMP_LT);
        } break;
        case GR_58: { // <operador_relacional> ::= ">"
            parser_stack_push_token(p, C_TOKEN_CMP_GT);
        } break;
        case GR_59: { // <aritmetica> ::= <termo> <aritmetica_mul>
            parser_stack_push_non_terminal(p, NT_ARITHMETIC_R);
            parser_stack_push_non_terminal(p, NT_TERM);
        } break;
        case GR_60: { // <aritmetica_mul> ::= "+" <termo> <aritmetica_mul>
            parser_stack_push_non_terminal(p, NT_ARITHMETIC_R);
            parser_stack_push_non_terminal(p, NT_TERM);
            parser_stack_push_token(p, C_TOKEN_ADD);
        } break;
        case GR_61: { // <aritmetica_mul> ::= "-" <termo> <aritmetica_mul>
            parser_stack_push_non_terminal(p, NT_ARITHMETIC_R);
            parser_stack_push_non_terminal(p, NT_TERM);
            parser_stack_push_token(p, C_TOKEN_SUB);
        } break;
        case GR_62: { // <aritmetica_mul> ::= î
        } break;
        case GR_63: { // <termo> ::= <fator> <termo_mul>
            parser_stack_push_non_terminal(p, NT_TERM_R);
            parser_stack_push_non_terminal(p, NT_FACTOR);
        } break;
        case GR_64: { // <termo_mul> ::= "*" <fator> <termo_mul>
            parser_stack_push_non_terminal(p, NT_TERM_R);
            parser_stack_push_non_terminal(p, NT_FACTOR);
            parser_stack_push_token(p, C_TOKEN_MUL);
        } break;
        case GR_65: { // <termo_mul> ::= "/" <fator> <termo_mul>
            parser_stack_push_non_terminal(p, NT_TERM_R);
            parser_stack_push_non_terminal(p, NT_FACTOR);
            parser_stack_push_token(p, C_TOKEN_DIV);
        } break;
        case GR_66: { // <termo_mul> ::= î
        } break;
        case GR_67: {  // <fator> ::= identificador
            parser_stack_push_token(p, C_TOKEN_IDENT);
        } break;
        case GR_68: {  // <fator> ::= constante_int
            parser_stack_push_token(p, C_TOKEN_INTEGER);
        } break;
        case GR_69: {  // <fator> ::= constante_float
            parser_stack_push_token(p, C_TOKEN_FLOAT);
        } break;
        case GR_70: { // <fator> ::= constante_string
            parser_stack_push_token(p, C_TOKEN_STRING);
        } break;
        case GR_71: { // <fator> ::= "(" <expr> ")"
            parser_stack_push_token(p, C_TOKEN_PAREN_CLOSE);
            parser_stack_push_non_terminal(p, NT_EXPR);
            parser_stack_push_token(p, C_TOKEN_PAREN_OPEN);
        } break;
        case GR_72: { // <fator> ::= "+" <fator>
            parser_stack_push_non_terminal(p, NT_FACTOR);
            parser_stack_push_token(p, C_TOKEN_ADD);
        } break;
        case GR_73: { // <fator> ::= "-" <fator>
            parser_stack_push_non_terminal(p, NT_FACTOR);
            parser_stack_push_token(p, C_TOKEN_SUB);
        } break;
        default: {
        } break;
        }
    }

    return ast;
}

static inline void parser_add_ast_node_from_non_terminal(Parser *p)
{
    CyAllocator a = p->ast->alloc;
    AstNode *cur = p->ast_cur;
    AstNode *new = NULL;
    ParserSymbol *top = parser_stack_peek(p);
    switch (top->u.non_terminal) {
    case NT_START: {
        new = AST_NODE_ALLOC(a, Main);
        new->kind = AST_MAIN;
        cur = new;
        p->ast->root = new;
    } break;
    case NT_INSTR_LIST: {
        CY_ASSERT(cur->kind == AST_MAIN);

        new = AST_NODE_ALLOC(a, StmtList);
        new->kind = AST_STMT_LIST;
        new->u.StmtList.stmts[0] = AST_NODE_ALLOC_ITEM(a, NULL, StmtList);

        cur->u.Main.body = new;
        cur = new;
    } break;
    case NT_INSTR_LIST_R: {
        CY_ASSERT(cur->kind == AST_STMT_LIST);

        cur = AST_NODE_ALLOC_ITEM(a, cur, StmtList);
    } break;
    case NT_INSTRUCTION: {
        CY_ASSERT(cur->kind == AST_STMT_LIST);
    } break;
    case NT_DEC_OR_ASSIGN: {
        CY_ASSERT(cur->kind == AST_STMT_LIST);

        new = AST_NODE_ALLOC(a, VarDecl);
        new->kind = AST_VAR_DECL;

        isize last = cur->u.StmtList.len - 1;
        cur->u.StmtList.stmts[last] = new;
    } break;
    case NT_ASSIGN_OPT: {

    } break;
    case NT_ID_LIST: {

    } break;
    case NT_ID_LIST_R: {

    } break;
    case NT_CMD: {

    } break;
    case NT_CMD_ASSIGN: {

    } break;
    case NT_CMD_INPUT: {

    } break;
    case NT_INPUT_LIST: {

    } break;
    case NT_INPUT_LIST_R: {

    } break;
    case NT_STRING_OPT: {

    } break;
    case NT_CMD_OUTPUT: {

    } break;
    case NT_CMD_OUTPUT_KEYWORD: {

    } break;
    case NT_CMD_COND: {

    } break;
    case NT_ELIF: {

    } break;
    case NT_ELSE: {

    } break;
    case NT_CMD_LIST: {

    } break;
    case NT_CMD_LIST_R: {

    } break;
    case NT_CMD_LOOP: {

    } break;
    case NT_CMD_LOOP_KEYWORD: {

    } break;
    case NT_EXPR_LIST: {

    } break;
    case NT_EXPR_LIST_R: {

    } break;
    case NT_EXPR: {

    } break;
    case NT_EXPR_LOG: {

    } break;
    case NT_ELEMENT: {

    } break;
    case NT_RELATIONAL: {

    } break;
    case NT_RELATIONAL_R: {

    } break;
    case NT_RELATIONAL_OP: {

    } break;
    case NT_ARITHMETIC: {

    } break;
    case NT_ARITHMETIC_R: {

    } break;
    case NT_TERM: {

    } break;
    case NT_TERM_R: {

    } break;
    case NT_FACTOR: {

    } break;
    default: break;
    }

    p->ast_cur = cur;
}

static inline void parser_add_ast_node_from_token(Parser *p)
{

}

CyString compile(String src_code)
{
#ifdef CY_DEBUG
    CyTicks start = cy_ticks_query();
#endif

    CyArena tokenizer_arena = cy_arena_init(cy_heap_allocator(), 0x4000);
    CyAllocator temp_allocator = cy_arena_allocator(&tokenizer_arena);
    CyAllocator heap_allocator = cy_heap_allocator();

    Tokenizer t = tokenizer_init(src_code);
    TokenList token_list = tokenize(temp_allocator, &t, true);
    if (t.err != T_ERR_NONE) {
        cy_free_all(temp_allocator);
        return tokenizer_create_error_msg(heap_allocator, &t);
    }

    isize stack_size = token_list.len * sizeof(ParserSymbol);
    CyStack parser_stack = cy_stack_init(heap_allocator, stack_size);
    CyAllocator stack_allocator = cy_stack_allocator(&parser_stack);
    Parser p = parser_init(stack_allocator, &token_list);
    Ast a = parse(temp_allocator, &p);
    if (p.err.kind != P_ERR_NONE) {
        return parser_create_error_msg(heap_allocator, &p);
    }

    CY_UNUSED(a); // TODO(cya): remove when the ast is implemented

    isize init_cap = 0x100;
    CyString output = cy_string_create_reserve(heap_allocator, init_cap);
    output = cy_string_append_c(output, "programa compilado com sucesso");

#ifdef CY_DEBUG
    CyTicks elapsed = cy_ticks_elapsed(start, cy_ticks_query());
    f64 elapsed_us = cy_ticks_to_time_unit(elapsed, CY_MICROSECONDS);
    output = cy_string_append_fmt(output, " em %.01fμs", elapsed_us);
#endif

    output = cy_string_shrink(output);

    cy_stack_deinit(&parser_stack);
    cy_arena_deinit(&tokenizer_arena);

    return output;
}
