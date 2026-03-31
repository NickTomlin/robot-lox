#include "scanner.h"
#include <ctype.h>

void scanner_init(Scanner *s, const char *source) {
    s->start   = source;
    s->current = source;
    s->line    = 1;
}

static bool is_at_end(Scanner *s) {
    return *s->current == '\0';
}

static char advance(Scanner *s) {
    return *s->current++;
}

static char peek(Scanner *s) {
    return *s->current;
}

static char peek_next(Scanner *s) {
    if (is_at_end(s)) return '\0';
    return s->current[1];
}

static bool match(Scanner *s, char expected) {
    if (is_at_end(s) || *s->current != expected) return false;
    s->current++;
    return true;
}

static Token make_token(Scanner *s, TokenType type) {
    return (Token){
        .type   = type,
        .start  = s->start,
        .length = (int)(s->current - s->start),
        .line   = s->line,
    };
}

static Token error_token(Scanner *s, const char *msg) {
    return (Token){
        .type   = TOKEN_ERROR,
        .start  = msg,
        .length = (int)strlen(msg),
        .line   = s->line,
    };
}

static void skip_whitespace(Scanner *s) {
    for (;;) {
        char c = peek(s);
        switch (c) {
            case ' ': case '\r': case '\t':
                advance(s);
                break;
            case '\n':
                s->line++;
                advance(s);
                break;
            case '/':
                if (peek_next(s) == '/') {
                    while (peek(s) != '\n' && !is_at_end(s)) advance(s);
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static TokenType check_keyword(Scanner *s, int start, int length,
                               const char *rest, TokenType type) {
    int total = (int)(s->current - s->start);
    if (total == start + length &&
        memcmp(s->start + start, rest, (size_t)length) == 0)
        return type;
    return TOKEN_IDENTIFIER;
}

static TokenType identifier_type(Scanner *s) {
    switch (s->start[0]) {
        case 'a': return check_keyword(s, 1, 2, "nd",    TOKEN_AND);
        case 'c': return check_keyword(s, 1, 4, "lass",  TOKEN_CLASS);
        case 'e': return check_keyword(s, 1, 3, "lse",   TOKEN_ELSE);
        case 'i': return check_keyword(s, 1, 1, "f",     TOKEN_IF);
        case 'n': return check_keyword(s, 1, 2, "il",    TOKEN_NIL);
        case 'o': return check_keyword(s, 1, 1, "r",     TOKEN_OR);
        case 'p': return check_keyword(s, 1, 4, "rint",  TOKEN_PRINT);
        case 'r': return check_keyword(s, 1, 5, "eturn", TOKEN_RETURN);
        case 's': return check_keyword(s, 1, 4, "uper",  TOKEN_SUPER);
        case 't':
            if (s->current - s->start > 1) {
                switch (s->start[1]) {
                    case 'h': return check_keyword(s, 2, 2, "is",  TOKEN_THIS);
                    case 'r': return check_keyword(s, 2, 2, "ue",  TOKEN_TRUE);
                }
            }
            break;
        case 'f':
            if (s->current - s->start > 1) {
                switch (s->start[1]) {
                    case 'a': return check_keyword(s, 2, 3, "lse", TOKEN_FALSE);
                    case 'o': return check_keyword(s, 2, 1, "r",   TOKEN_FOR);
                    case 'u': return check_keyword(s, 2, 1, "n",   TOKEN_FUN);
                }
            }
            break;
        case 'v': return check_keyword(s, 1, 2, "ar",   TOKEN_VAR);
        case 'w': return check_keyword(s, 1, 4, "hile", TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token scan_identifier(Scanner *s) {
    while (isalnum((unsigned char)peek(s)) || peek(s) == '_') advance(s);
    return make_token(s, identifier_type(s));
}

static Token scan_number(Scanner *s) {
    while (isdigit((unsigned char)peek(s))) advance(s);
    if (peek(s) == '.' && isdigit((unsigned char)peek_next(s))) {
        advance(s); // consume '.'
        while (isdigit((unsigned char)peek(s))) advance(s);
    }
    return make_token(s, TOKEN_NUMBER);
}

static Token scan_string(Scanner *s) {
    while (peek(s) != '"' && !is_at_end(s)) {
        if (peek(s) == '\n') s->line++;
        advance(s);
    }
    if (is_at_end(s)) return error_token(s, "Unterminated string.");
    advance(s); // closing "
    return make_token(s, TOKEN_STRING);
}

Token scanner_next(Scanner *s) {
    skip_whitespace(s);
    s->start = s->current;

    if (is_at_end(s)) return make_token(s, TOKEN_EOF);

    char c = advance(s);

    if (isalpha((unsigned char)c) || c == '_') return scan_identifier(s);
    if (isdigit((unsigned char)c))              return scan_number(s);

    switch (c) {
        case '(': return make_token(s, TOKEN_LEFT_PAREN);
        case ')': return make_token(s, TOKEN_RIGHT_PAREN);
        case '{': return make_token(s, TOKEN_LEFT_BRACE);
        case '}': return make_token(s, TOKEN_RIGHT_BRACE);
        case ';': return make_token(s, TOKEN_SEMICOLON);
        case ',': return make_token(s, TOKEN_COMMA);
        case '.': return make_token(s, TOKEN_DOT);
        case '-': return make_token(s, TOKEN_MINUS);
        case '+': return make_token(s, TOKEN_PLUS);
        case '/': return make_token(s, TOKEN_SLASH);
        case '*': return make_token(s, TOKEN_STAR);
        case '!': return make_token(s, match(s, '=') ? TOKEN_BANG_EQUAL    : TOKEN_BANG);
        case '=': return make_token(s, match(s, '=') ? TOKEN_EQUAL_EQUAL   : TOKEN_EQUAL);
        case '<': return make_token(s, match(s, '=') ? TOKEN_LESS_EQUAL    : TOKEN_LESS);
        case '>': return make_token(s, match(s, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"': return scan_string(s);
    }

    return error_token(s, "Unexpected character.");
}
