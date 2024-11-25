#define CY_IMPLEMENTATION
#include "cy.h"

typedef CyStringView String;

#define STRING_ARG(s) (int)s.len, s.text

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

static inline void tokenizer_parse_numeric_constant(
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
        b32 not_keyword = rune_is_uppercase(token.str.text[0]) ||
            cy_string_view_contains(token.str, "_0123456789");
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
            tokenizer_parse_numeric_constant(t, &token);
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
                    b32 invalid = (cur_rune == '&' || cur_rune == '|') &&
                        next_rune != cur_rune;
                    if (invalid) {
                        tokenizer_error(t, &token, T_ERR_INVALID_SYMBOL, NULL);
                    } else {
                        tokenizer_advance_to_next_rune(t);
                        token.kind = token_kind_from_string(s);
                    }
                }
            } else {
                tokenizer_advance_to_next_rune(t);
                token.kind = token_kind_from_string(s);
                if (token.kind == C_TOKEN_INVALID) {
                    tokenizer_error(t, &token, T_ERR_INVALID_SYMBOL, NULL);
                }
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

static inline CyString append_error_prefix(CyString str, TokenPos err_pos)
{
    return cy_string_append_fmt(str, "Erro na linha %td – ", err_pos.line);
}

static CyString tokenizer_append_error_msg(CyString msg, const Tokenizer *t)
{
    if (t->err == T_ERR_OUT_OF_MEMORY) {
        return msg; // NOTE(cya): since we're out of memory
    }

    msg = append_error_prefix(msg, t->bad_tok.pos);

    TokenizerError err = t->err;
    if (err != T_ERR_INVALID_STRING && err != T_ERR_INVALID_COMMENT) {
        msg = cy_string_append_fmt(msg, "%.*s ", STRING_ARG(t->bad_tok.str));
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
        desc = "comentário de bloco inválido ou não finalizado";
    } break;
    default: {
        desc = "(fatal) erro não reconhecido ao tokenizar código";
    } break;
    }

    msg = cy_string_append_c(msg, desc);

#if 0
    if (t->err_desc != NULL) {
        msg = cy_string_append_fmt(msg, " (%s)", t->err_desc);
    }
#endif

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

typedef struct {
    f64 val;
    isize precision;
} AstFloat;

typedef enum {
    AST_ENT_INT,
    AST_ENT_FLOAT,
    AST_ENT_STRING,
    AST_ENT_BOOL,
} AstEntityKind;

typedef struct {
    AstEntityKind kind;
    union {
        isize i;
        AstFloat f;
        String s;
        b32 b;
    } u;
} AstLiteral;

#define AST_KINDS \
    AST_KIND(IDENT, struct { \
        Token tok; \
        AstEntityKind kind; \
    }) \
    AST_KIND(LITERAL, struct { \
        Token tok; \
        AstLiteral val; \
    }) \
    AST_KIND(MAIN, struct { \
        AstNode *body; \
    }) \
\
AST_KIND(_LIST_BEGIN, isize) \
    AST_KIND(IDENT_LIST, struct { \
        AstList list; \
    }) \
    AST_KIND(INPUT_LIST, struct { \
        AstList list; \
    }) \
    AST_KIND(EXPR_LIST, struct { \
        AstList list; \
    }) \
    AST_KIND(STMT_LIST, struct { \
        AstList list; \
    }) \
    AST_KIND(INPUT_ARG, struct { \
        AstNode *prompt; \
        AstNode *ident; \
    }) \
    AST_KIND(INPUT_PROMPT, struct { \
        Token string; \
    }) \
    AST_KIND(VAR_DECL, struct { \
        AstNode *ident_list; \
    }) \
AST_KIND(_LIST_END, isize) \
\
AST_KIND(_STMT_BEGIN, isize) \
    AST_KIND(ASSIGN_STMT, struct { \
        AstNode *ident_list; \
        AstNode *expr; \
    }) \
    AST_KIND(READ_STMT, struct { \
        AstNode *input_list; \
    }) \
    AST_KIND(WRITE_STMT, struct { \
        Token keyword; \
        AstNode *expr_list; \
    }) \
    AST_KIND(IF_STMT, struct { \
        AstNode *body; \
        AstNode *cond; \
        AstNode *else_stmt; \
        b32 is_root; \
    }) \
    AST_KIND(REPEAT_STMT, struct { \
        AstNode *body; \
        Token keyword; \
        AstNode *expr; \
    }) \
AST_KIND(_STMT_END, isize) \
\
AST_KIND(_EXPR_BEGIN, isize) \
    AST_KIND(BINARY_EXPR, struct { \
        AstNode *left; \
        AstNode *right; \
        Token op; \
    }) \
    AST_KIND(UNARY_EXPR, struct { \
        AstNode *expr; \
        Token op; \
    }) \
    AST_KIND(PAREN_EXPR, struct { \
        AstNode *expr; \
    }) \
AST_KIND(_EXPR_END, isize) \
    AST_KIND(KIND_COUNT, isize)

#define IS_IN_RANGE_IN(n, lo, hi) (n >= lo && n <= hi)
#define IS_IN_RANGE_EX(n, lo, hi) (n > lo && n < hi)

#define AST_KIND_IS_OF_CLASS(kind, c) \
    IS_IN_RANGE_EX(kind, AST_KIND__##c##_BEGIN, AST_KIND__##c##_END)

#define AST_KIND_PREFIX(t) AST_KIND_##t
typedef enum {
#define AST_KIND(e, ...) AST_KIND_PREFIX(e),
    AST_KINDS
#undef AST_KIND
} AstKind;

typedef struct AstNode AstNode;

typedef struct {
    CyAllocator alloc;
    AstNode **data;
    isize len;
    isize cap;
} AstList;

#define AST_LIST_INIT_CAP 0x10

#define AST_PREFIX(t) AST_##t
#define AST_KIND(t, s) typedef s AST_PREFIX(t);
    AST_KINDS
#undef AST_KIND

struct AstNode {
    AstKind kind;
    union {
#define AST_KIND(t, ...) AST_PREFIX(t) t;
        AST_KINDS
#undef AST_KIND
    } u;
};

typedef struct {
    CyAllocator alloc;
    AstNode *root;
} Ast;

// TODO(cya): maybe alloc only the size of the required kind?
#define AST_NODE_ALLOC(alloc, k) ast_node_alloc(alloc, AST_KIND_PREFIX(k))

static inline AstNode *ast_node_alloc(CyAllocator a, AstKind kind)
{
    AstNode *node = cy_alloc(a, sizeof(AstNode));
    CY_VALIDATE_PTR(node);

    node->kind = kind;
    return node;
}

static inline AstList ast_list_init(CyAllocator a)
{
    isize cap = AST_LIST_INIT_CAP;
    AstNode **data = cy_alloc(a, cap * sizeof(AstNode*));
    return (AstList){
        .alloc = a,
        .data = data,
        .cap = data == NULL ? 0 : cap,
    };
}

#define AST_LIST_CREATE(alloc, _list, new, _kind) { \
    new = *_list; \
    if (*_list == NULL) { \
        new = AST_NODE_ALLOC(alloc, _kind); \
        new->u._kind.list = ast_list_init(alloc); \
        *_list = new; \
    }  \
} (void)0

static inline void ast_list_append_node(AstList *l, AstNode *node)
{
    if (l->len == l->cap) {
        isize new_cap = l->cap * 2;
        AstNode **new = cy_resize(
            l->alloc, l->data,
            l->cap * sizeof(AstNode*), new_cap * sizeof(AstNode*)
        );
        l->data = new;
        l->cap = new_cap;
    }

    l->data[l->len++] = node;
}

static inline AstNode *ast_list_get_last_node(AstList *l)
{
    return l->data[l->len - 1];
}

static inline void ast_list_shrink(AstList *l)
{
    if (l->len == l->cap) {
        return;
    }

    isize new_cap = l->len;
    AstNode **new = cy_resize(
        l->alloc, l->data,
        l->cap * sizeof(AstNode*), new_cap * sizeof(AstNode*)
    );
    l->data = new;
    l->cap = new_cap;
}

static inline void ast_expr_insert_node(AstNode *expr, AstNode *node)
{
    switch (expr->kind) {
    case AST_KIND_UNARY_EXPR:
    case AST_KIND_PAREN_EXPR: {
        AST_PAREN_EXPR *e = &expr->u.PAREN_EXPR;
        CY_ASSERT(e->expr == NULL);

        e->expr = node;
    } break;
    case AST_KIND_BINARY_EXPR: {
        AST_BINARY_EXPR *e = &expr->u.BINARY_EXPR;
        CY_ASSERT(e->left == NULL || e->right == NULL);

        if (e->left == NULL) {
            e->left = node;
        } else {
            e->right = node;
        }
    } break;
    default: break;
    }
}

static inline AstEntityKind ast_entity_kind_from_ident(Token *ident_tok)
{
    String ident = ident_tok->str;
    AstEntityKind kind = -1;
    if (cy_string_view_has_prefix(ident, "i_")) {
        kind = AST_ENT_INT;
    } else if (cy_string_view_has_prefix(ident, "f_")) {
        kind = AST_ENT_FLOAT;
    } else if (cy_string_view_has_prefix(ident, "b_")) {
        kind = AST_ENT_BOOL;
    } else {
        kind = AST_ENT_STRING;
    }

    return kind;
}

static inline AstEntityKind ast_expr_determine_kind(AstNode *expr)
{
    AstEntityKind kind = -1;
    AstKind expr_kind = expr->kind;
    switch (expr_kind) {
    case AST_KIND_BINARY_EXPR: {
        AstEntityKind lhs = ast_expr_determine_kind(expr->u.BINARY_EXPR.left);
        AstEntityKind rhs = ast_expr_determine_kind(expr->u.BINARY_EXPR.right);

        Token op = expr->u.BINARY_EXPR.op;
        switch (op.kind) {
        case C_TOKEN_ADD:
        case C_TOKEN_SUB:
        case C_TOKEN_MUL: {
            if (lhs == AST_ENT_INT && rhs == AST_ENT_INT) {
                kind = AST_ENT_INT;
            } else {
                kind = AST_ENT_FLOAT;
            }
        } break;
        case C_TOKEN_DIV: {
            kind = AST_ENT_FLOAT;
        } break;
        case C_TOKEN_CMP_EQ:
        case C_TOKEN_CMP_NE:
        case C_TOKEN_CMP_GT:
        case C_TOKEN_CMP_LT: {
            kind = lhs;
        } break;
        case C_TOKEN_AND:
        case C_TOKEN_OR: {
            kind = AST_ENT_BOOL;
        } break;
        default: break;
        }
    } break;
    case AST_KIND_UNARY_EXPR: {
        if (expr->u.UNARY_EXPR.op.kind == C_TOKEN_NOT) {
            kind = AST_ENT_BOOL;
        } else {
            kind = ast_expr_determine_kind(expr->u.UNARY_EXPR.expr);
        }
    } break;
    case AST_KIND_PAREN_EXPR: {
        kind = ast_expr_determine_kind(expr->u.PAREN_EXPR.expr);
    } break;
    case AST_KIND_IDENT: {
        kind = ast_entity_kind_from_ident(&expr->u.IDENT.tok);
    } break;
    case AST_KIND_LITERAL: {
        kind = expr->u.LITERAL.val.kind;
    } break;
    default: break;
    }

    return kind;
}

static inline void ast_binary_expr_reduce(CyAllocator a, AstNode *expr)
{
    CY_ASSERT(expr->kind == AST_KIND_BINARY_EXPR);

    AST_BINARY_EXPR *e = &expr->u.BINARY_EXPR;
    CY_ASSERT(e->right == NULL);

    AstNode *child = e->left;
    // if (child->kind == AST_KIND_BINARY_EXPR) {
    //     child->u.BINARY_EXPR.op = e->op;
    // }

    cy_mem_copy(expr, child, sizeof(*expr));
    cy_free(a, child);
}

static inline isize parse_int(const Token *tok)
{
    CY_ASSERT(tok->kind == C_TOKEN_INTEGER);

    isize res = 0;
    const u8 *start = tok->str.text, *cur = start + tok->str.len - 1;
    for (isize mul = 1; cur >= start; mul *= 10) {
        res += (*cur-- - '0') * mul;
    }

    return res;
}

static inline f64 pow(f64 n, f64 exp)
{
    f64 mul = n;
    while (--exp) {
        n *= mul;
    }

    return n;
 }

static inline AstFloat parse_float(const Token *tok)
{
    CY_ASSERT(tok->kind == C_TOKEN_FLOAT);

    isize len = 0;
    const u8 *start, *end;
    end = start = tok->str.text;
    while (*end != ',') {
        end += 1, len += 1;
    }

    f64 whole_part = (f64)parse_int(&(Token){
        .kind = C_TOKEN_INTEGER,
        .str = cy_string_view_create_len((const char*)start, len),
    });

    f64 div = 10.0;
    start = end + 1;
    end = tok->str.text + tok->str.len;

    len = end - start;
    f64 decimal_part = (f64)parse_int(&(Token){
        .kind = C_TOKEN_INTEGER,
        .str = cy_string_view_create_len((const char*)start, len),
    });

    return (AstFloat){
        .val = whole_part + decimal_part / pow(div, len),
        .precision = len,
    };
}

// TODO(cya): move this to the code generator section
#if 0
static inline CyString parse_string(CyAllocator a, const Token *tok)
{
    CY_ASSERT(tok->kind == C_TOKEN_STRING);

    CyString str = cy_string_create_view(a, tok->str);

#ifdef CY_OS_WINDOWS
    // TODO(cya): convert from UTF-8 to ANSI Windows codepage (CP_ACP)
    isize utf16_len = MultiByteToWideChar(
        CP_UTF8, 0, str, cy_string_len(str), NULL, 0
    );
    wchar_t *utf16 = cy_alloc(a, (utf16_len + 1) * sizeof(*utf16));
    MultiByteToWideChar(CP_UTF8, 0, str, cy_string_len(str), utf16, utf16_len);

    isize ansi_len = WideCharToMultiByte(
        CP_OEMCP, 0, utf16, utf16_len, NULL, 0, NULL, NULL
    );
    CyString ansi = cy_string_create_reserve(a, ansi_len + 1);
    WideCharToMultiByte(
        CP_OEMCP, 0, utf16, utf16_len, ansi, ansi_len, NULL, NULL
    );
    cy__string_set_len(ansi, ansi_len);

    cy_string_free(str);
    cy_free(a, utf16);
    str = ansi;
#endif

    return str;
}
#endif

static inline void ast_node_read_token(AstNode *node, Token *tok)
{
    if (node == NULL) {
        return;
    }

    Token *dest = NULL;
    switch (node->kind) {
    case AST_KIND_IDENT_LIST: {
        if (tok->kind != C_TOKEN_IDENT) {
            return;
        }

        AstNode *ident_node = ast_list_get_last_node(&node->u.IDENT_LIST.list);
        ident_node->u.IDENT.kind = ast_entity_kind_from_ident(tok);
        dest = &ident_node->u.IDENT.tok;
    } break;
    case AST_KIND_INPUT_LIST: {
        if (tok->kind != C_TOKEN_IDENT) {
            return;
        }

        AstNode *arg_node = ast_list_get_last_node(&node->u.INPUT_LIST.list);
        arg_node->u.INPUT_ARG.ident->u.IDENT.kind =
            ast_entity_kind_from_ident(tok);
        dest = &arg_node->u.INPUT_ARG.ident->u.IDENT.tok;
    } break;
    case AST_KIND_INPUT_PROMPT: {
        if (tok->kind != C_TOKEN_STRING) {
            return;
        }

        dest = &node->u.INPUT_PROMPT.string;
    } break;
    case AST_KIND_WRITE_STMT: {
        if (!IS_IN_RANGE_IN(tok->kind, C_TOKEN_WRITE, C_TOKEN_WRITELN)) {
            return;
        }

        dest = &node->u.WRITE_STMT.keyword;
    } break;
    case AST_KIND_REPEAT_STMT: {
        if (!IS_IN_RANGE_IN(tok->kind, C_TOKEN_REPEAT, C_TOKEN_UNTIL)) {
            return;
        }

        dest = &node->u.REPEAT_STMT.keyword;
    } break;
    case AST_KIND_LITERAL: {
        switch (tok->kind) {
        case C_TOKEN_INTEGER: {
            node->u.LITERAL.val.kind = AST_ENT_INT;
            node->u.LITERAL.val.u.i = parse_int(tok);
        } break;
        case C_TOKEN_FLOAT: {
            node->u.LITERAL.val.kind = AST_ENT_FLOAT;
            node->u.LITERAL.val.u.f = parse_float(tok);
        } break;
        case C_TOKEN_STRING: {
            node->u.LITERAL.val.kind = AST_ENT_STRING;
            node->u.LITERAL.val.u.s = tok->str;
        } break;
        case C_TOKEN_TRUE: {
            node->u.LITERAL.val.kind = AST_ENT_BOOL;
            node->u.LITERAL.val.u.b = true;
        } break;
        case C_TOKEN_FALSE: {
            node->u.LITERAL.val.kind = AST_ENT_BOOL;
            node->u.LITERAL.val.u.b = false;
        } break;
        default: return;
        }

        dest = &node->u.LITERAL.tok;
    } break;
    case AST_KIND_IDENT: {
        if (tok->kind != C_TOKEN_IDENT) {
            return;
        }

        node->u.IDENT.kind = ast_entity_kind_from_ident(tok);
        dest = &node->u.IDENT.tok;
    } break;
    case AST_KIND_UNARY_EXPR: {
        dest = &node->u.UNARY_EXPR.op;
    } break;
    default: return;
    }

    cy_mem_copy(dest, tok, sizeof(*dest));
}

typedef struct {
    AstNode *ast_entry;
    b32 is_frame_start;
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

// NOTE(cya): equivalent to alignof(ParserSymbol)
#define PARSER_STACK_ALIGN (sizeof(Token))

typedef enum {
    P_ERR_OUT_OF_MEMORY = -1,
    P_ERR_NONE,
    P_ERR_REACHED_EOF,
    P_ERR_UNEXPECTED_TOKEN,
    P_ERR_INVALID_RULE,
} ParserErrorKind;

typedef struct {
    ParserErrorKind kind;
    ParserSymbol expected;
    Token found;
} ParserError;

typedef struct {
    Token *read_tok;
    ParserStack stack;
    Ast *ast;
    AstNode *cur_node;
    ParserError err;
} Parser;

static void parser_error(Parser *p, ParserErrorKind kind);
static inline ParserSymbol *parser_stack_peek(Parser *p);

static inline void parser_stack_push(
    Parser *p, ParserSymbol item, AstNode *ast_entry
) {
    if (p->stack.len == p->stack.cap) {
        isize old_size = p->stack.cap * sizeof(*p->stack.items);
        isize new_size = old_size * 2;
        ParserSymbol *items = cy_resize_align(
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

    item.ast_entry = ast_entry == NULL ? p->cur_node : ast_entry;
    p->stack.items[p->stack.len++] = item;
}

static inline void parser_stack_push_token(
    Parser *p, TokenKind kind, AstNode *ast_entry
) {
    parser_stack_push(p, (ParserSymbol){
        .kind = PARSER_KIND_TOKEN,
        .u.token = (Token){.kind = kind, .str = g_token_strings[kind]},
    }, ast_entry);
}

static inline void parser_stack_push_non_terminal(
    Parser *p, NonTerminal n, AstNode *ast_entry
) {
    parser_stack_push(p, (ParserSymbol){
        .kind = PARSER_KIND_NON_TERMINAL,
        .u.non_terminal = n,
    }, ast_entry);
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

static inline b32 parser_stack_is_empty(Parser *p)
{
    return p->stack.len < 1;
}

static inline void parser_stack_mark_top_as_frame(Parser *p)
{
    ParserSymbol *s = parser_stack_peek(p);
    s->is_frame_start = true;
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
        str = cy_string_append_fmt(str, "%.*s ", STRING_ARG(s));
    }

    int len = cy_string_len(str);
    str[len - 1] = '\0';
    cy__string_set_len(str, len - 1);
    str = cy_string_shrink(str);

    return str;
}

static CyString parser_append_error_msg(CyString msg, Parser *p)
{
    msg = append_error_prefix(msg, p->err.found.pos);

    CyAllocator a = CY_STRING_HEADER(msg)->alloc;
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
        STRING_ARG(found_str),
        expected_str
    );
    cy_string_free(expected_str);

    return msg;
}

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
    parser_stack_push_token(&p, C_TOKEN_EOF, NULL);
    parser_stack_push_non_terminal(&p, NT_START, NULL);

    return p;
}

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

            ast_node_read_token(p->cur_node, p->read_tok);
            parser_stack_pop(p);

            p->read_tok += 1;
            continue;
        } else if (stack_top->is_frame_start) {
            parser_stack_pop(p);
            if (!parser_stack_is_empty(p)) {
                p->cur_node = parser_stack_peek(p)->ast_entry;
            }

            continue;
        }

        u8 table_row = g_ll1_row_from_kind[stack_top->u.non_terminal];
        u8 table_col = g_ll1_col_from_kind[p->read_tok->kind];
        GrammarRule rule = g_ll1_table[table_row][table_col];
        if (RULE_IS_INVALID(rule)) {
            parser_error(p, P_ERR_INVALID_RULE);
            break;
        }

        parser_stack_mark_top_as_frame(p);

        AstNode *new_node = p->cur_node;
        switch (rule) {
        case GR_0: { // <inicio> ::= main <lista_instr> end
            parser_stack_push_token(p, C_TOKEN_END, NULL);
            parser_stack_push_non_terminal(p, NT_INSTR_LIST, NULL);
            parser_stack_push_token(p, C_TOKEN_MAIN, NULL);

            CY_ASSERT(p->cur_node == NULL);

            new_node = AST_NODE_ALLOC(a, MAIN);
            ast.root = new_node;
        } break;
        case GR_1: { // <lista_instr> ::= <instrucao> ";" <lista_instr_rep>
            parser_stack_push_non_terminal(p, NT_INSTR_LIST_R, NULL);
            parser_stack_push_token(p, C_TOKEN_SEMICOLON, NULL);
            parser_stack_push_non_terminal(p, NT_INSTRUCTION, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_MAIN);

            AstNode **body = &(p->cur_node->u.MAIN.body);
            AST_LIST_CREATE(a, body, new_node, STMT_LIST);
        } break;
        case GR_2: { // <lista_instr_rep> ::= <lista_instr>
            parser_stack_push_non_terminal(p, NT_INSTR_LIST, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_MAIN);
        } break;
        case GR_3: { // <lista_instr_rep> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_MAIN);

            AstList *l = &p->cur_node->u.MAIN.body->u.STMT_LIST.list;
            ast_list_shrink(l);
        } break;
        case GR_4: { // <instrucao> ::= <dec_ou_atr>
            parser_stack_push_non_terminal(p, NT_DEC_OR_ASSIGN, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            // NOTE(cya): assuming assignment first
            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, ASSIGN_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_5: { // <instrucao> ::= <cmd_entr>
            parser_stack_push_non_terminal(p, NT_CMD_INPUT, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, READ_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_6: { // <instrucao> ::= <cmd_saida>
            parser_stack_push_non_terminal(p, NT_CMD_OUTPUT, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, WRITE_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_7: { // <instrucao> ::= <cmd_rep>
            parser_stack_push_non_terminal(p, NT_CMD_LOOP, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, REPEAT_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_8: { // <instrucao> ::= <cmd_sel>
            parser_stack_push_non_terminal(p, NT_CMD_COND, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, IF_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_9: { // <dec_ou_atr> ::= <lista_id> <atr_opt>
            parser_stack_push_non_terminal(p, NT_ASSIGN_OPT, NULL);
            parser_stack_push_non_terminal(p, NT_ID_LIST, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_ASSIGN_STMT);

            AstNode **ident_list = &p->cur_node->u.ASSIGN_STMT.ident_list;
            AST_LIST_CREATE(a, ident_list, new_node, IDENT_LIST);
        } break;
        case GR_10: { // <atr_opt> ::= "=" <expr>
            parser_stack_push_non_terminal(p, NT_EXPR, NULL);
            parser_stack_push_token(p, C_TOKEN_EQUALS, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_ASSIGN_STMT);
        } break;
        case GR_11: { // <atr_opt> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_ASSIGN_STMT);

            p->cur_node->kind = AST_KIND_VAR_DECL;
        } break;
        case GR_12: { // <lista_id> ::= identificador <lista_id_mul>
            parser_stack_push_non_terminal(p, NT_ID_LIST_R, NULL);
            parser_stack_push_token(p, C_TOKEN_IDENT, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_IDENT_LIST);

            AstNode *ident_node = AST_NODE_ALLOC(a, IDENT);
            AstList *l = &p->cur_node->u.IDENT_LIST.list;
            ast_list_append_node(l, ident_node);
        } break;
        case GR_13: { // <lista_id_mul> ::= "," <lista_id>
            parser_stack_push_non_terminal(p, NT_ID_LIST, NULL);
            parser_stack_push_token(p, C_TOKEN_COMMA, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_IDENT_LIST);
        } break;
        case GR_14: { // <lista_id_mul> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_IDENT_LIST);

            AstList *l = &p->cur_node->u.IDENT_LIST.list;
            ast_list_shrink(l);
        } break;
        case GR_15: { // <cmd> ::= <cmd_atr>
            parser_stack_push_non_terminal(p, NT_CMD_ASSIGN, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, ASSIGN_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_16: { // <cmd> ::= <cmd_entr>
            parser_stack_push_non_terminal(p, NT_CMD_INPUT, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, READ_STMT);
            ast_list_append_node(l, new_node);
        } break;
         case GR_17: { // <cmd> ::= <cmd_saida>
            parser_stack_push_non_terminal(p, NT_CMD_OUTPUT, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, WRITE_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_18: { // <cmd> ::= <cmd_rep>
            parser_stack_push_non_terminal(p, NT_CMD_LOOP, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, REPEAT_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_19: { // <cmd> ::= <cmd_sel>
            parser_stack_push_non_terminal(p, NT_CMD_COND, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_STMT_LIST);

            AstList *l = &p->cur_node->u.STMT_LIST.list;
            new_node = AST_NODE_ALLOC(a, IF_STMT);
            ast_list_append_node(l, new_node);
        } break;
        case GR_20: { // <cmd_atr> ::= <lista_id> "=" <expr>
            parser_stack_push_non_terminal(p, NT_EXPR, NULL);
            parser_stack_push_token(p, C_TOKEN_EQUALS, NULL);
            parser_stack_push_non_terminal(p, NT_ID_LIST, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_ASSIGN_STMT);

            AstNode **ident_list = &p->cur_node->u.ASSIGN_STMT.ident_list;
            AST_LIST_CREATE(a, ident_list, new_node, IDENT_LIST);
        } break;
        case GR_21: { // <cmd_entr> ::= read "(" <lista_entr> ")"
            parser_stack_push_token(p, C_TOKEN_PAREN_CLOSE, NULL);
            parser_stack_push_non_terminal(p, NT_INPUT_LIST, NULL);
            parser_stack_push_token(p, C_TOKEN_PAREN_OPEN, NULL);
            parser_stack_push_token(p, C_TOKEN_READ, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_READ_STMT);

            AstNode **input_list = &(p->cur_node->u.READ_STMT.input_list);
            AST_LIST_CREATE(a, input_list, new_node, INPUT_LIST);
        } break;
        case GR_22: { // <lista_entr> ::= <cte_str_opt> id <lista_entr_mul>
            parser_stack_push_non_terminal(p, NT_INPUT_LIST_R, NULL);
            parser_stack_push_token(p, C_TOKEN_IDENT, NULL);
            parser_stack_push_non_terminal(p, NT_STRING_OPT, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_INPUT_LIST);

            new_node = AST_NODE_ALLOC(a, INPUT_ARG);
            new_node->u.INPUT_ARG.ident = AST_NODE_ALLOC(a, IDENT);

            AstList *l = &p->cur_node->u.INPUT_LIST.list;
            ast_list_append_node(l, new_node);
        } break;
        case GR_23: { // <lista_entr_mul> ::= "," <lista_entr>
            parser_stack_push_non_terminal(p, NT_INPUT_LIST, NULL);
            parser_stack_push_token(p, C_TOKEN_COMMA, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_INPUT_LIST);
        } break;
        case GR_24: { // <lista_entr_mul> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_INPUT_LIST);

            AstList *l = &p->cur_node->u.INPUT_LIST.list;
            ast_list_shrink(l);
        } break;
        case GR_25: { // <cte_str_opt> ::= constante_string ","
            parser_stack_push_token(p, C_TOKEN_COMMA, NULL);
            parser_stack_push_token(p, C_TOKEN_STRING, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_INPUT_ARG);

            new_node = AST_NODE_ALLOC(a, INPUT_PROMPT);
            p->cur_node->u.INPUT_ARG.prompt = new_node;
        } break;
        case GR_26: { // <cte_str_opt> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_INPUT_ARG);
        } break;
        case GR_27: { // <cmd_saida> ::= <cmd_saida_tipo> "(" <lista_expr> ")"
            parser_stack_push_token(p, C_TOKEN_PAREN_CLOSE, NULL);
            parser_stack_push_non_terminal(p, NT_EXPR_LIST, NULL);
            parser_stack_push_token(p, C_TOKEN_PAREN_OPEN, NULL);
            parser_stack_push_non_terminal(p, NT_CMD_OUTPUT_KEYWORD, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_WRITE_STMT);
        } break;
        case GR_28: { // <cmd_saida_tipo> ::= write
            parser_stack_push_token(p, C_TOKEN_WRITE, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_WRITE_STMT);
        } break;
        case GR_29: { // <cmd_saida_tipo> ::= writeln
            parser_stack_push_token(p, C_TOKEN_WRITELN, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_WRITE_STMT);
        } break;
        case GR_30: { // <lista_expr> ::= <expr> <lista_expr_mul>
            parser_stack_push_non_terminal(p, NT_EXPR_LIST_R, NULL);
            parser_stack_push_non_terminal(p, NT_EXPR, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_WRITE_STMT);

            AstNode **expr_list = &p->cur_node->u.WRITE_STMT.expr_list;
            AST_LIST_CREATE(a, expr_list, new_node, EXPR_LIST);
        } break;
        case GR_31: { // <lista_expr_mul> ::= "," <lista_expr>
            parser_stack_push_non_terminal(p, NT_EXPR_LIST, NULL);
            parser_stack_push_token(p, C_TOKEN_COMMA, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_WRITE_STMT);
        } break;
        case GR_32: { // <lista_expr_mul> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_WRITE_STMT);

            AstList *l = &p->cur_node->u.WRITE_STMT.expr_list->u.EXPR_LIST.list;
            ast_list_shrink(l);
        } break;
        case GR_33: { // <cmd_sel> ::= if <expr> <lista_cmd> <elif> <else> end
            parser_stack_push_token(p, C_TOKEN_END, NULL);
            parser_stack_push_non_terminal(p, NT_ELSE, NULL);
            parser_stack_push_non_terminal(p, NT_ELIF, NULL);
            parser_stack_push_non_terminal(p, NT_CMD_LIST, NULL);
            parser_stack_push_non_terminal(p, NT_EXPR, NULL);
            parser_stack_push_token(p, C_TOKEN_IF, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_IF_STMT);

            p->cur_node->u.IF_STMT.is_root = true;
        } break;
        case GR_34: { // <elif> ::= elif <expr> <lista_cmd> <elif>
            new_node = AST_NODE_ALLOC(a, IF_STMT);

            parser_stack_push_non_terminal(p, NT_ELIF, new_node);
            parser_stack_push_non_terminal(p, NT_CMD_LIST, new_node);
            parser_stack_push_non_terminal(p, NT_EXPR, new_node);
            parser_stack_push_token(p, C_TOKEN_ELIF, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_IF_STMT);

            AstNode *else_stmt = p->cur_node->u.IF_STMT.else_stmt;
            while (else_stmt != NULL) {
                p->cur_node = else_stmt;
                else_stmt = p->cur_node->u.IF_STMT.else_stmt;
            }

            p->cur_node->u.IF_STMT.else_stmt = new_node;
        } break;
        case GR_35: { // <elif> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_IF_STMT);
        } break;
        case GR_36: { // <else> ::= else <lista_cmd>
            new_node = AST_NODE_ALLOC(a, IF_STMT);

            parser_stack_push_non_terminal(p, NT_CMD_LIST, new_node);
            parser_stack_push_token(p, C_TOKEN_ELSE, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_IF_STMT);

            AstNode *else_stmt = p->cur_node->u.IF_STMT.else_stmt;
            while (else_stmt != NULL) {
                p->cur_node = else_stmt;
                else_stmt = p->cur_node->u.IF_STMT.else_stmt;
            }

            p->cur_node->u.IF_STMT.else_stmt = new_node;
        } break;
        case GR_37: { // <else> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_IF_STMT);
        } break;
        case GR_38: { // <lista_cmd> ::= <cmd> ";" <lista_cmd_mul>
            parser_stack_push_non_terminal(p, NT_CMD_LIST_R, NULL);
            parser_stack_push_token(p, C_TOKEN_SEMICOLON, NULL);
            parser_stack_push_non_terminal(p, NT_CMD, NULL);

            CY_ASSERT(
                p->cur_node->kind == AST_KIND_IF_STMT ||
                p->cur_node->kind == AST_KIND_REPEAT_STMT
            );

            AstNode **body = &p->cur_node->u.IF_STMT.body;
            AST_LIST_CREATE(a, body, new_node, STMT_LIST);
        } break;
        case GR_39: { // <lista_cmd_mul> ::= <lista_cmd>
            parser_stack_push_non_terminal(p, NT_CMD_LIST, NULL);

            CY_ASSERT(
                p->cur_node->kind == AST_KIND_IF_STMT ||
                p->cur_node->kind == AST_KIND_REPEAT_STMT
            );
        } break;
        case GR_40: { // <lista_cmd_mul> ::= î
            CY_ASSERT(
                p->cur_node->kind == AST_KIND_IF_STMT ||
                p->cur_node->kind == AST_KIND_REPEAT_STMT
            );

            AstList *l = &p->cur_node->u.IF_STMT.body->u.STMT_LIST.list;
            ast_list_shrink(l);
        } break;
        case GR_41: { // <cmd_rep> ::= repeat <lista_cmd> <cmd_rep_tipo> <expr>
            parser_stack_push_non_terminal(p, NT_EXPR, NULL);
            parser_stack_push_non_terminal(p, NT_CMD_LOOP_KEYWORD, NULL);
            parser_stack_push_non_terminal(p, NT_CMD_LIST, NULL);
            parser_stack_push_token(p, C_TOKEN_REPEAT, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_REPEAT_STMT);
        } break;
        case GR_42: { // <cmd_rep_tipo> ::= while
            parser_stack_push_token(p, C_TOKEN_WHILE, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_REPEAT_STMT);
        } break;
        case GR_43: { // <cmd_rep_tipo> ::= until
            parser_stack_push_token(p, C_TOKEN_UNTIL, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_REPEAT_STMT);
        } break;
        case GR_44: { // <expr> ::= <elemento> <expr_log>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_EXPR_LOG, new_node);
            parser_stack_push_non_terminal(p, NT_ELEMENT, new_node);

            CY_ASSERT(
                p->cur_node->kind == AST_KIND_ASSIGN_STMT ||
                p->cur_node->kind == AST_KIND_IF_STMT ||
                p->cur_node->kind == AST_KIND_REPEAT_STMT ||
                p->cur_node->kind == AST_KIND_EXPR_LIST ||
                p->cur_node->kind == AST_KIND_PAREN_EXPR
            );

            switch (p->cur_node->kind) {
            case AST_KIND_ASSIGN_STMT: {
                p->cur_node->u.ASSIGN_STMT.expr = new_node;
            } break;
            case AST_KIND_IF_STMT: {
                p->cur_node->u.IF_STMT.cond = new_node;
            } break;
            case AST_KIND_REPEAT_STMT: {
                p->cur_node->u.REPEAT_STMT.expr = new_node;
            } break;
            case AST_KIND_EXPR_LIST: {
                AstList *l = &p->cur_node->u.EXPR_LIST.list;
                ast_list_append_node(l, new_node);
            } break;
            case AST_KIND_PAREN_EXPR: {
                p->cur_node->u.PAREN_EXPR.expr = new_node;
            } break;
            default: break;
            }
        } break;
        case GR_45: { // <expr_log> ::= "&&" <elemento> <expr_log>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_EXPR_LOG, new_node);
            parser_stack_push_non_terminal(p, NT_ELEMENT, new_node);
            parser_stack_push_token(p, C_TOKEN_AND, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_46: { // <expr_log> ::= "||" <elemento> <expr_log>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_EXPR_LOG, new_node);
            parser_stack_push_non_terminal(p, NT_ELEMENT, new_node);
            parser_stack_push_token(p, C_TOKEN_OR, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_47: { // <expr_log> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_binary_expr_reduce(a, p->cur_node);
        } break;
        case GR_48: { // <elemento> ::= <relacional>
            parser_stack_push_non_terminal(p, NT_RELATIONAL, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));
        } break;
        case GR_49: { // <elemento> ::= true
            parser_stack_push_token(p, C_TOKEN_TRUE, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            new_node = AST_NODE_ALLOC(a, LITERAL);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_50: { // <elemento> ::= false
            parser_stack_push_token(p, C_TOKEN_FALSE, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            new_node = AST_NODE_ALLOC(a, LITERAL);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_51: { // <elemento> ::= "!" <elemento>
            new_node = AST_NODE_ALLOC(a, UNARY_EXPR);
            parser_stack_push_non_terminal(p, NT_ELEMENT, new_node);
            parser_stack_push_token(p, C_TOKEN_NOT, new_node);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_52: { // <relacional> ::= <aritmetica> <relacional_mul>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_RELATIONAL_R, new_node);
            parser_stack_push_non_terminal(p, NT_ARITHMETIC, new_node);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_53: { // <relacional_mul> ::= <operador_relacional> <aritmetica>
            parser_stack_push_non_terminal(p, NT_ARITHMETIC, NULL);
            parser_stack_push_non_terminal(p, NT_RELATIONAL_OP, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);
        } break;
        case GR_54: { // <relacional_mul> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_binary_expr_reduce(a, p->cur_node);
        } break;
        case GR_55: { // <operador_relacional> ::= "=="
            parser_stack_push_token(p, C_TOKEN_CMP_EQ, NULL);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);
        } break;
        case GR_56: { // <operador_relacional> ::= "!="
            parser_stack_push_token(p, C_TOKEN_CMP_NE, NULL);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);
        } break;
        case GR_57: { // <operador_relacional> ::= "<"
            parser_stack_push_token(p, C_TOKEN_CMP_LT, NULL);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);
        } break;
        case GR_58: { // <operador_relacional> ::= ">"
            parser_stack_push_token(p, C_TOKEN_CMP_GT, NULL);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);
        } break;
        case GR_59: { // <aritmetica> ::= <termo> <aritmetica_mul>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_ARITHMETIC_R, new_node);
            parser_stack_push_non_terminal(p, NT_TERM, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_60: { // <aritmetica_mul> ::= "+" <termo> <aritmetica_mul>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_ARITHMETIC_R, new_node);
            parser_stack_push_non_terminal(p, NT_TERM, new_node);
            parser_stack_push_token(p, C_TOKEN_ADD, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_61: { // <aritmetica_mul> ::= "-" <termo> <aritmetica_mul>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_ARITHMETIC_R, new_node);
            parser_stack_push_non_terminal(p, NT_TERM, new_node);
            parser_stack_push_token(p, C_TOKEN_SUB, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_62: { // <aritmetica_mul> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_binary_expr_reduce(a, p->cur_node);
        } break;
        case GR_63: { // <termo> ::= <fator> <termo_mul>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_TERM_R, new_node);
            parser_stack_push_non_terminal(p, NT_FACTOR, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_64: { // <termo_mul> ::= "*" <fator> <termo_mul>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_TERM_R, new_node);
            parser_stack_push_non_terminal(p, NT_FACTOR, new_node);
            parser_stack_push_token(p, C_TOKEN_MUL, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_65: { // <termo_mul> ::= "/" <fator> <termo_mul>
            new_node = AST_NODE_ALLOC(a, BINARY_EXPR);

            parser_stack_push_non_terminal(p, NT_TERM_R, new_node);
            parser_stack_push_non_terminal(p, NT_FACTOR, new_node);
            parser_stack_push_token(p, C_TOKEN_DIV, new_node);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            p->cur_node->u.BINARY_EXPR.op = *p->read_tok;
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_66: { // <termo_mul> ::= î
            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            ast_binary_expr_reduce(a, p->cur_node);
        } break;
        case GR_67: { // <fator> ::= identificador
            parser_stack_push_token(p, C_TOKEN_IDENT, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            new_node = AST_NODE_ALLOC(a, IDENT);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_68: { // <fator> ::= constante_int
            parser_stack_push_token(p, C_TOKEN_INTEGER, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            new_node = AST_NODE_ALLOC(a, LITERAL);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_69: { // <fator> ::= constante_float
            parser_stack_push_token(p, C_TOKEN_FLOAT, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            new_node = AST_NODE_ALLOC(a, LITERAL);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_70: { // <fator> ::= constante_string
            parser_stack_push_token(p, C_TOKEN_STRING, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            new_node = AST_NODE_ALLOC(a, LITERAL);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_71: { // <fator> ::= "(" <expr> ")"
            parser_stack_push_token(p, C_TOKEN_PAREN_CLOSE, NULL);
            parser_stack_push_non_terminal(p, NT_EXPR, NULL);
            parser_stack_push_token(p, C_TOKEN_PAREN_OPEN, NULL);

            CY_ASSERT(AST_KIND_IS_OF_CLASS(p->cur_node->kind, EXPR));

            new_node = AST_NODE_ALLOC(a, PAREN_EXPR);

            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_72: { // <fator> ::= "+" <fator>
            parser_stack_push_non_terminal(p, NT_FACTOR, NULL);
            parser_stack_push_token(p, C_TOKEN_ADD, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            new_node = AST_NODE_ALLOC(a, UNARY_EXPR);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        case GR_73: { // <fator> ::= "-" <fator>
            parser_stack_push_non_terminal(p, NT_FACTOR, NULL);
            parser_stack_push_token(p, C_TOKEN_SUB, NULL);

            CY_ASSERT(p->cur_node->kind == AST_KIND_BINARY_EXPR);

            new_node = AST_NODE_ALLOC(a, UNARY_EXPR);
            ast_expr_insert_node(p->cur_node, new_node);
        } break;
        default: {
        } break;
        }

        p->cur_node = new_node;
    }

    return ast;
}

/* ----------------------------- Checker ------------------------------------ */
typedef enum {
    C_ERR_NONE,
    C_ERR_UNDECLARED_IDENT,
    C_ERR_REDECLARED_IDENT,
    C_ERR_INVALID_TYPE,
} CheckerError;

typedef struct {
    CheckerError err;
    Token tok;
    Token op;
} CheckerStatus;

static inline b32 is_declared(AstList *decl_idents, Token *ident)
{
    String tok = ident->str;
    for (isize i = 0; i < decl_idents->len; i++) {
        String other = decl_idents->data[i]->u.IDENT.tok.str;
        if (cy_string_view_are_equal(tok, other)) {
            return true;
        }
    }

    return false;
}

static inline CheckerStatus checker_error(CheckerError err, Token *tok)
{
    CheckerStatus status = { .err = err };
    if (tok != NULL) {
        status.tok = *tok;
    }

    return status;
}

static CheckerStatus check_expr(AstNode *expr, AstList *decl_idents)
{
    switch (expr->kind) {
    case AST_KIND_BINARY_EXPR: {
        CheckerStatus lhs = check_expr(expr->u.BINARY_EXPR.left, decl_idents);
        if (lhs.err != C_ERR_NONE) {
            return lhs;
        }

        CheckerStatus rhs = check_expr(expr->u.BINARY_EXPR.right, decl_idents);
        if (rhs.err != C_ERR_NONE) {
            return rhs;
        }
    } break;
    case AST_KIND_UNARY_EXPR:
    case AST_KIND_PAREN_EXPR: {
        AstNode *sub = expr->u.UNARY_EXPR.expr;
        if (AST_KIND_IS_OF_CLASS(sub->kind, EXPR)) {
            return check_expr(sub, decl_idents);
        } else switch (sub->kind) {
        case AST_KIND_IDENT: {
            return check_expr(sub, decl_idents);
        } break;
        default: break;
        }
    } break;
    case AST_KIND_IDENT: {
        Token *tok = &expr->u.IDENT.tok;
        if (!is_declared(decl_idents, tok)) {
            return checker_error(C_ERR_UNDECLARED_IDENT, tok);
        }
    } break;
    default: break;
    }

    return checker_error(C_ERR_NONE, NULL);
}

static CheckerStatus check_stmt(AstNode *node, AstList *decl_idents)
{
    CheckerStatus status = {0};
    switch (node->kind) {
    case AST_KIND_VAR_DECL: {
        AstList *idents = &node->u.VAR_DECL.ident_list->u.IDENT_LIST.list;
        for (isize i = 0; i < idents->len; i++) {
            AstNode *ident = idents->data[i];
            Token *tok = &ident->u.IDENT.tok;
            if (is_declared(decl_idents, tok)) {
                return checker_error(C_ERR_REDECLARED_IDENT, tok);
            }

            ast_list_append_node(decl_idents, ident);
        }
    } break;
    case AST_KIND_ASSIGN_STMT: {
        AstList *idents = &node->u.ASSIGN_STMT.ident_list->u.IDENT_LIST.list;
        for (isize i = 0; i < idents->len; i++) {
            AstNode *ident = idents->data[i];
            Token *tok = &ident->u.IDENT.tok;
            if (!is_declared(decl_idents, tok)) {
                return checker_error(C_ERR_UNDECLARED_IDENT, tok);
            }
        }

        return check_expr(node->u.ASSIGN_STMT.expr, decl_idents);
    } break;
    case AST_KIND_READ_STMT: {
        AstList *inputs = &node->u.READ_STMT.input_list->u.INPUT_LIST.list;
        for (isize i = 0; i < inputs->len; i++) {
            Token *tok = &inputs->data[i]->u.INPUT_ARG.ident->u.IDENT.tok;
            if (!is_declared(decl_idents, tok)) {
                return checker_error(C_ERR_UNDECLARED_IDENT, tok);
            }
        }
    } break;
    case AST_KIND_WRITE_STMT: {
        AstList *exprs = &node->u.WRITE_STMT.expr_list->u.EXPR_LIST.list;
        for (isize i = 0; i < exprs->len; i++) {
            AstNode *expr = exprs->data[i];
            status = check_expr(expr, decl_idents);
            if (status.err != C_ERR_NONE) {
                return status;
            }
        }
    } break;
    case AST_KIND_IF_STMT: {
        AstNode *cond = node->u.IF_STMT.cond;
        if (cond != NULL) {
            status = check_expr(cond, decl_idents);
            if (status.err != C_ERR_NONE) {
                return status;
            }
        }

        AstList *stmts = &node->u.IF_STMT.body->u.STMT_LIST.list;
        for (isize i = 0; i < stmts->len; i++) {
            AstNode *stmt = stmts->data[i];
            status = check_stmt(stmt, decl_idents);
            if (status.err != C_ERR_NONE) {
                return status;
            }
        }

        AstNode *else_stmt = node->u.IF_STMT.else_stmt;
        if (else_stmt != NULL) {
            status = check_stmt(else_stmt, decl_idents);
        }
    } break;
    case AST_KIND_REPEAT_STMT: {
        AstNode *expr = node->u.REPEAT_STMT.expr;
        if (expr != NULL) {
            status = check_expr(expr, decl_idents);
            if (status.err != C_ERR_NONE) {
                return status;
            }
        }

        AstList *stmts = &node->u.REPEAT_STMT.body->u.STMT_LIST.list;
        for (isize i = 0; i < stmts->len; i++) {
            AstNode *stmt = stmts->data[i];
            status = check_stmt(stmt, decl_idents);
            if (status.err != C_ERR_NONE) {
                return status;
            }
        }
    } break;
    default: break;
    }

    return status;
}

static CheckerStatus check(Ast *a)
{
    CheckerStatus status = {0};
    AstList decl_idents = ast_list_init(a->alloc);

    AstList *stmts = &a->root->u.MAIN.body->u.STMT_LIST.list;
    for (isize i = 0; i < stmts->len; i++) {
        AstNode *stmt = stmts->data[i];
        status = check_stmt(stmt, &decl_idents);
        if (status.err != C_ERR_NONE) {
            break;
        }
    }

    return status;
}

static inline CyString checker_append_error_msg(CyString msg, CheckerStatus *s)
{
    msg = append_error_prefix(msg, s->tok.pos);
    msg = cy_string_append_fmt(msg, "%.*s ", STRING_ARG(s->tok.str));

    switch (s->err) {
    case C_ERR_UNDECLARED_IDENT: {
        msg = cy_string_append_c(msg, "não declarado");
    } break;
    case C_ERR_REDECLARED_IDENT: {
        msg = cy_string_append_c(msg, "já declarado");
    } break;
    case C_ERR_INVALID_TYPE: {
    } break;
    case C_ERR_NONE: {
    } break;
    }

    return msg;
}

/* ------------------------- Code Generator (MSIL) -------------------------- */
typedef struct {
    CyAllocator alloc;
    isize *items;
    isize len;
    isize cap;
} LabelStack;

typedef struct {
    CyAllocator alloc;
    Ast *ast;
    CyString code;
    LabelStack labels;
} IlGenerator;

static inline isize *label_stack_peek(IlGenerator *g)
{
    CY_VALIDATE_PTR(g);
    return (g->labels.len > 0) ? &g->labels.items[g->labels.len - 1] : NULL;
}

static inline b32 label_stack_is_empty(IlGenerator *g)
{
    return g->labels.len < 1;
}

static inline isize label_stack_push(IlGenerator *g)
{
    if (g->labels.len == g->labels.cap) {
        isize old_size = g->labels.cap * sizeof(*g->labels.items);
        isize new_size = old_size * 2;
        isize *items = cy_resize(
            g->labels.alloc, g->labels.items,
            old_size, new_size
        );
        if (items == NULL) {
            // TODO(cya): error out here? (FIXME)
            // _error(g, P_ERR_OUT_OF_MEMORY);
            return -1;
        }

        g->labels.items = items;
        g->labels.cap *= 2;
    }

     isize item = label_stack_is_empty(g) ? 1 : *label_stack_peek(g) + 1;
     g->labels.items[g->labels.len++] = item;
     return item;
}

static inline isize label_stack_pop(IlGenerator *g)
{
    if (g->labels.len <= 0) {
        return -1;
    }

    isize *top = &g->labels.items[--g->labels.len];
    isize ret = *top;

    cy_mem_set(top, 0, sizeof(*top));
    return ret;
}

static inline IlGenerator il_generator_init(
    CyAllocator a, CyAllocator stack_allocator, Ast *ast
) {
    isize cap = 0x10;
    isize *items = cy_alloc(stack_allocator, cap);
    return (IlGenerator){
        .alloc = a,
        .ast = ast,
        .labels = (LabelStack){
            .alloc = stack_allocator,
            .items = items,
            .cap = cap,
        },
    };
}

static void il_generator_append_line(IlGenerator *g, const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);

    char buf[0x1000] = {0};
    vsnprintf(buf, sizeof(buf), fmt, va);

    va_end(va);

    g->code = cy_string_append_fmt(g->code, "\t\t%s\r\n", buf);
}

static inline void il_generator_append_expr(IlGenerator *g, AstNode *expr)
{
    AstKind expr_kind = expr->kind;
    switch (expr_kind) {
    case AST_KIND_BINARY_EXPR: {
        il_generator_append_expr(g, expr->u.BINARY_EXPR.left);
        il_generator_append_expr(g, expr->u.BINARY_EXPR.right);

        const char *instr = NULL;
        Token op = expr->u.BINARY_EXPR.op;
        switch (op.kind) {
        case C_TOKEN_ADD: {
            instr = "add";
        } break;
        case C_TOKEN_SUB: {
            instr = "sub";
        } break;
        case C_TOKEN_MUL: {
            instr = "mul";
        } break;
        case C_TOKEN_DIV: {
            instr = "div";
        } break;
        case C_TOKEN_CMP_EQ: {
            instr = "ceq";
        } break;
        case C_TOKEN_CMP_NE: {
            instr = "cne";
        } break;
        case C_TOKEN_CMP_GT: {
            instr = "cgt";
        } break;
        case C_TOKEN_CMP_LT: {
            instr = "clt";
        } break;
        case C_TOKEN_AND: {
            instr = "and";
        } break;
        case C_TOKEN_OR: {
            instr = "or";
        } break;
        default: break;
        }

        il_generator_append_line(g, instr);
    } break;
    case AST_KIND_UNARY_EXPR: {
        il_generator_append_expr(g, expr->u.UNARY_EXPR.expr);

        Token op = expr->u.UNARY_EXPR.op;
        if (op.kind == C_TOKEN_NOT) {
            il_generator_append_line(g, "not");
        } else if (op.kind == C_TOKEN_SUB) {
            il_generator_append_line(g, "ldc.r8 -1.0");
            il_generator_append_line(g, "mul");
        }
    } break;
    case AST_KIND_PAREN_EXPR: {
        il_generator_append_expr(g, expr->u.PAREN_EXPR.expr);
    } break;
    case AST_KIND_IDENT: {
        String name = expr->u.IDENT.tok.str;
        il_generator_append_line(g, "ldloc %.*s", STRING_ARG(name));
        if (expr->u.IDENT.kind == AST_ENT_INT) {
            il_generator_append_line(g, "conv.r8");
        }
    } break;
    case AST_KIND_LITERAL: {
        AstEntityKind kind = expr->u.LITERAL.val.kind;
        if (kind== AST_ENT_STRING) {
            String s = expr->u.LITERAL.val.u.s;
            il_generator_append_line(g, "ldstr %.*s", STRING_ARG(s));
        } else {
            char buf[0x100] = {0};
            isize buf_size = sizeof(buf);
            const char *kind_id = NULL;
            switch (kind) {
            case AST_ENT_INT: {
                kind_id = "i8";
                isize val = expr->u.LITERAL.val.u.i;
                snprintf(buf, buf_size, "%td", val);
            } break;
            case AST_ENT_FLOAT: {
                kind_id = "r8";
                f64 val = expr->u.LITERAL.val.u.f.val;
                isize precision = expr->u.LITERAL.val.u.f.precision;
                snprintf(buf, buf_size, "%.*lf", (int)precision, val);
            } break;
            case AST_ENT_BOOL: {
                kind_id = "i4";
                b32 val = expr->u.LITERAL.val.u.b;
                snprintf(buf, buf_size, "%d", val);
            } break;
            default: break;
            }

            il_generator_append_line(g, "ldc.%s %s", kind_id, buf);
            if (kind == AST_ENT_INT) {
                il_generator_append_line(g, "conv.r8");
            }
        }
    } break;
    default: break;
    }
}

static inline const char *il_keyword_from_entity_kind(AstEntityKind kind)
{
    const char *keyword = NULL;
    switch (kind) {
    case AST_ENT_INT: {
        keyword = "int64";
    } break;
    case AST_ENT_FLOAT: {
        keyword = "float64";
    } break;
    case AST_ENT_BOOL: {
        keyword = "bool";
    } break;
    case AST_ENT_STRING: {
        keyword = "string";
    } break;
    }

    return keyword;
}
static inline const char *il_class_from_entity_kind(AstEntityKind kind)
{
    const char *keyword = NULL;
    switch (kind) {
    case AST_ENT_INT: {
        keyword = "Int64";
    } break;
    case AST_ENT_FLOAT: {
        keyword = "Double";
    } break;
    case AST_ENT_BOOL: {
        keyword = "Bool";
    } break;
    case AST_ENT_STRING: {
        keyword = "string";
    } break;
    }

    return keyword;
}

static inline void il_generator_append_label(IlGenerator *g)
{
    isize label = label_stack_pop(g);
    g->code = cy_string_append_fmt(g->code, "IL_%02td:\r\n", label);
}

static inline void il_generator_append_stmt(IlGenerator *g, AstNode *stmt)
{
    switch (stmt->kind) {
    case AST_KIND_VAR_DECL: {
        AstList *idents = &stmt->u.VAR_DECL.ident_list->u.IDENT_LIST.list;
        for (isize i = 0; i < idents->len; i++) {
            AstNode *ident = idents->data[i];
            const char *kind = il_keyword_from_entity_kind(ident->u.IDENT.kind);

            String name = ident->u.IDENT.tok.str;
            il_generator_append_line(
                g, ".locals (%s %.*s)", kind, STRING_ARG(name)
            );
        }
    } break;
    case AST_KIND_ASSIGN_STMT: {
        AstNode *expr = stmt->u.ASSIGN_STMT.expr;
        // TODO(cya): determine if we're even gonna need this in the first place
        AstEntityKind expr_kind = ast_expr_determine_kind(expr);
        il_generator_append_expr(g, expr);
        if (expr_kind == AST_ENT_INT) {
            il_generator_append_line(g, "conv.i8");
        }

        AstList *idents = &stmt->u.ASSIGN_STMT.ident_list->u.IDENT_LIST.list;
        for (isize i = 0; i < idents->len - 1; i++) {
            il_generator_append_line(g, "dup");
        }

        for (isize i = 0; i < idents->len; i++) {
            AstNode *ident = idents->data[i];
            String name = ident->u.IDENT.tok.str;
            il_generator_append_line(g, "stloc %.*s", STRING_ARG(name));
        }
    } break;
    case AST_KIND_READ_STMT: {
        AstList *args = &stmt->u.READ_STMT.input_list->u.INPUT_LIST.list;
        for (isize i = 0; i < args->len; i++) {
            AstNode *arg = args->data[i];
            String prompt = arg->u.INPUT_ARG.prompt->u.INPUT_PROMPT.string.str;
            il_generator_append_line(g, "ldstr %.*s", STRING_ARG(prompt));
            il_generator_append_line(
                g, "call void [mscorlib]System.Console::Write(string)"
            );

            String ident = arg->u.INPUT_ARG.ident->u.IDENT.tok.str;
            AstEntityKind kind = arg->u.INPUT_ARG.ident->u.IDENT.kind;
            il_generator_append_line(
                g, "call string [mscorlib]System.Console::ReadLine()"
            );
            if (kind != AST_ENT_STRING) {
                const char *keyword = il_keyword_from_entity_kind(kind);
                const char *class = il_class_from_entity_kind(kind);
                il_generator_append_line(
                    g, "call %s [mscorlib]System.%s::Parse(string)",
                    keyword, class
                );
            }

            il_generator_append_line(g, "stloc %.*s", STRING_ARG(ident));
        }
    } break;
    case AST_KIND_WRITE_STMT: {
        AstList *exprs = &stmt->u.WRITE_STMT.expr_list->u.EXPR_LIST.list;
        for (isize i = 0; i < exprs->len; i++) {
            AstNode *expr = exprs->data[i];
            AstEntityKind expr_kind = ast_expr_determine_kind(expr);
            il_generator_append_expr(g, expr);

            const char *kind = il_keyword_from_entity_kind(expr_kind);
            if (expr_kind == AST_ENT_INT) {
                il_generator_append_line(g, "conv.i8");
            }

            il_generator_append_line(
                g, "call void [mscorlib]System.Console::Write(%s)", kind
            );
        }

        if (stmt->u.WRITE_STMT.keyword.kind == C_TOKEN_WRITELN) {
            il_generator_append_line(
                g, "call void [mscorlib]System.Console::WriteLine()"
            );
        }
    } break;
    case AST_KIND_IF_STMT: {
        isize end_label = 0;
        b32 is_root = stmt->u.IF_STMT.is_root;
        if (is_root) {
            end_label = label_stack_push(g);
        }

        isize else_label = label_stack_push(g);
        isize if_label = label_stack_push(g);

        AstNode *expr = stmt->u.IF_STMT.cond;
        if (expr != NULL) {
            il_generator_append_expr(g, expr);
            il_generator_append_line(g, "brtrue IL_%02td", if_label);
            il_generator_append_line(g, "br IL_%02td", else_label);

            il_generator_append_label(g);
        } else {
            label_stack_pop(g);
        }

        AstList *stmts = &stmt->u.IF_STMT.body->u.STMT_LIST.list;
        for (isize i = 0; i < stmts->len; i++) {
            il_generator_append_stmt(g, stmts->data[i]);
        }

        il_generator_append_line(g, "br IL_%02td", end_label);

        if (expr != NULL) {
            il_generator_append_label(g);
            g->code = cy_string_append_fmt(g->code, "IL_%02td:\r\n", if_label);
        }

        AstNode *else_stmt = stmt->u.IF_STMT.else_stmt;
        if (else_stmt != NULL) {
            il_generator_append_stmt(g, else_stmt);
        }

        if (is_root) {
            il_generator_append_label(g);
        }
    } break;
    case AST_KIND_REPEAT_STMT: {
    } break;
    default: break;
    }
}

static CyString il_generate(IlGenerator *g)
{
    AstList *stmts = &g->ast->root->u.MAIN.body->u.STMT_LIST.list;
    isize init_cap = 10 * stmts->len;
    g->code = cy_string_create_reserve(g->alloc, init_cap);
    const char *header =
        ".assembly extern mscorlib {}\r\n"
        ".assembly _obj_code {}\r\n"
        ".module _obj_code.exe\r\n"
        ".class public Main {\r\n"
        "\t.method public static void main() {\r\n"
        "\t\t.entrypoint\r\n";
    g->code = cy_string_append_c(g->code, header);

    for (isize i = 0; i < stmts->len; i++) {
        AstNode *stmt = stmts->data[i];
        il_generator_append_stmt(g, stmt);
    }

    const char *footer =
        "\t\tret\r\n"
        "\t}\r\n"
        "}\r\n";
    g->code = cy_string_append_c(g->code, footer);

    return g->code;
}

typedef struct {
    CyString msg;
    CyString code;
} CompilerOutput;

void compiler_output_free(CompilerOutput *output)
{
    cy_string_free(output->msg);
    cy_string_free(output->code);
    cy_mem_zero(output, sizeof(*output));
}

CompilerOutput compile(CyAllocator a, String src_code)
{
#ifdef CY_DEBUG
    CyTicks start = cy_ticks_query();
#endif

    CyArena tokenizer_arena = cy_arena_init(a, 0x4000);
    CyAllocator temp_allocator = cy_arena_allocator(&tokenizer_arena);

    CyStack parser_stack = {0};

    CyString code = NULL;
    isize init_cap = 0x100;
    CyString msg = cy_string_create_reserve(a, init_cap);
    Tokenizer tokenizer = tokenizer_init(src_code);
    TokenList token_list = tokenize(temp_allocator, &tokenizer, true);
    if (tokenizer.err != T_ERR_NONE) {
        msg = tokenizer_append_error_msg(msg, &tokenizer);
        goto cleanup;
    }

    isize stack_size = token_list.len * sizeof(ParserSymbol);
    parser_stack = cy_stack_init(a, stack_size);
    CyAllocator stack_allocator = cy_stack_allocator(&parser_stack);
    Parser parser = parser_init(stack_allocator, &token_list);

    // TODO(cya): use pool allocator when implemented
    Ast ast = parse(temp_allocator, &parser);
    if (parser.err.kind != P_ERR_NONE) {
        msg = parser_append_error_msg(msg, &parser);
        goto cleanup;
    }

    CheckerStatus status = check(&ast);
    if (status.err != C_ERR_NONE) {
        msg = checker_append_error_msg(msg, &status);
        goto cleanup;
    }

    IlGenerator generator = il_generator_init(a, stack_allocator, &ast);
    code = il_generate(&generator);
    msg = cy_string_append_c(msg, "programa compilado com sucesso");

#ifdef CY_DEBUG
    CyTicks elapsed = cy_ticks_elapsed(start, cy_ticks_query());
    f64 elapsed_us = cy_ticks_to_time_unit(elapsed, CY_MICROSECONDS);
    msg = cy_string_append_fmt(msg, " em %.01fμs", elapsed_us);
#endif

cleanup:
    cy_stack_deinit(&parser_stack);
    cy_arena_deinit(&tokenizer_arena);

    return (CompilerOutput){
        .code = code,
        .msg = cy_string_shrink(msg),
    };
}
