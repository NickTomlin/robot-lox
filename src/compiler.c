#include "compiler.h"
#include "scanner.h"
#include "object.h"
#include "gc.h"
#include "memory.h"

// ============================================================
// Data structures
// ============================================================

typedef struct {
    Token current;
    Token previous;
    bool  had_error;
    bool  panic_mode;
} Parser;

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool can_assign);

typedef struct {
    ParseFn    prefix;
    ParseFn    infix;
    Precedence precedence;
} ParseRule;

typedef struct {
    Token name;
    int   depth;        // scope depth, -1 = unresolved (declared but not defined)
    bool  is_captured;  // captured by a closure
} Local;

typedef struct {
    uint8_t index;    // local slot or upvalue index in enclosing
    bool    is_local; // true = captures a local; false = captures an upvalue
} Upvalue;

typedef enum {
    TYPE_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
    struct Compiler *enclosing;
    ObjFunction     *function;
    FunctionType     type;

    Local    locals[UINT8_COUNT];
    int      local_count;
    Upvalue  upvalues[UINT8_COUNT];
    int      scope_depth;
} Compiler;

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
    bool has_superclass;
} ClassCompiler;

// ---- Globals ----
static Parser         parser;
static Scanner        scanner;
static Compiler      *current    = NULL;
static ClassCompiler *current_class = NULL;

// ============================================================
// Forward declarations
// ============================================================
static void expression(void);
static void statement(void);
static void declaration(void);
static ParseRule *get_rule(TokenType type);
static void parse_precedence(Precedence prec);

// ============================================================
// Error helpers
// ============================================================

static void error_at(Token *token, const char *msg) {
    if (parser.panic_mode) return;
    parser.panic_mode = true;
    fprintf(stderr, "[line %d] Error", token->line);
    if (token->type == TOKEN_EOF)
        fprintf(stderr, " at end");
    else if (token->type != TOKEN_ERROR)
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    fprintf(stderr, ": %s\n", msg);
    parser.had_error = true;
}

static void error(const char *msg)          { error_at(&parser.previous, msg); }
static void error_at_current(const char *msg) { error_at(&parser.current, msg); }

// ============================================================
// Token helpers
// ============================================================

static void advance_token(void) {
    parser.previous = parser.current;
    for (;;) {
        parser.current = scanner_next(&scanner);
        if (parser.current.type != TOKEN_ERROR) break;
        error_at_current(parser.current.start);
    }
}

static void consume(TokenType type, const char *msg) {
    if (parser.current.type == type) { advance_token(); return; }
    error_at_current(msg);
}

static bool check(TokenType type)   { return parser.current.type == type; }
static bool match_tok(TokenType t)  { if (!check(t)) return false; advance_token(); return true; }

// ============================================================
// Emit helpers
// ============================================================

static Chunk *current_chunk(void) { return &current->function->chunk; }

static void emit_byte(uint8_t byte) {
    chunk_write(current_chunk(), byte, parser.previous.line);
}

static void emit_bytes(uint8_t a, uint8_t b) { emit_byte(a); emit_byte(b); }

static void emit_loop(int loop_start) {
    emit_byte(OP_LOOP);
    int offset = current_chunk()->count - loop_start + 2;
    if (offset > UINT16_MAX) error("Loop body too large.");
    emit_byte((uint8_t)((offset >> 8) & 0xff));
    emit_byte((uint8_t)(offset        & 0xff));
}

static int emit_jump(uint8_t instruction) {
    emit_byte(instruction);
    emit_byte(0xff); emit_byte(0xff); // placeholder
    return current_chunk()->count - 2;
}

static void patch_jump(int offset) {
    int jump = current_chunk()->count - offset - 2;
    if (jump > UINT16_MAX) error("Too much code to jump over.");
    current_chunk()->code[offset]     = (uint8_t)((jump >> 8) & 0xff);
    current_chunk()->code[offset + 1] = (uint8_t)(jump        & 0xff);
}

static void emit_return(void) {
    if (current->type == TYPE_INITIALIZER)
        emit_bytes(OP_GET_LOCAL, 0);
    else
        emit_byte(OP_NIL);
    emit_byte(OP_RETURN);
}

static uint8_t make_constant(Value value) {
    int idx = chunk_add_constant(current_chunk(), value);
    if (idx > UINT8_MAX) { error("Too many constants in one chunk."); return 0; }
    return (uint8_t)idx;
}

static void emit_constant(Value value) {
    emit_bytes(OP_CONSTANT, make_constant(value));
}

// ============================================================
// Compiler lifecycle
// ============================================================

static void compiler_init(Compiler *c, FunctionType type) {
    c->enclosing   = current;
    c->function    = NULL;
    c->type        = type;
    c->local_count = 0;
    c->scope_depth = 0;
    c->function    = obj_function_new();
    current        = c;

    if (type != TYPE_SCRIPT)
        current->function->name = obj_string_copy(parser.previous.start,
                                                   parser.previous.length);
    // Slot 0 reserved for 'this' (methods) or implicit function object
    Local *local = &current->locals[current->local_count++];
    local->depth      = 0;
    local->is_captured = false;
    if (type != TYPE_FUNCTION) {
        local->name.start  = "this";
        local->name.length = 4;
    } else {
        local->name.start  = "";
        local->name.length = 0;
    }
}

static ObjFunction *compiler_end(void) {
    emit_return();
    ObjFunction *fn = current->function;
#if DEBUG_PRINT_CODE
    if (!parser.had_error) {
        // (disassembly omitted for brevity — add debug_chunk_disassemble if needed)
        (void)fn;
    }
#endif
    current = current->enclosing;
    return fn;
}

// ============================================================
// Scope / locals / upvalues
// ============================================================

static void scope_begin(void) { current->scope_depth++; }

static void scope_end(void) {
    current->scope_depth--;
    while (current->local_count > 0 &&
           current->locals[current->local_count - 1].depth > current->scope_depth) {
        if (current->locals[current->local_count - 1].is_captured)
            emit_byte(OP_CLOSE_UPVALUE);
        else
            emit_byte(OP_POP);
        current->local_count--;
    }
}

static bool identifiers_equal(Token *a, Token *b) {
    return a->length == b->length &&
           memcmp(a->start, b->start, (size_t)a->length) == 0;
}

static int resolve_local(Compiler *c, Token *name) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        Local *l = &c->locals[i];
        if (identifiers_equal(name, &l->name)) {
            if (l->depth == -1) error("Can't read local variable in its own initializer.");
            return i;
        }
    }
    return -1;
}

static int add_upvalue(Compiler *c, uint8_t index, bool is_local) {
    int count = c->function->upvalue_count;
    for (int i = 0; i < count; i++) {
        Upvalue *uv = &c->upvalues[i];
        if (uv->index == index && uv->is_local == is_local) return i;
    }
    if (count == UINT8_COUNT) { error("Too many closure variables in function."); return 0; }
    c->upvalues[count].is_local = is_local;
    c->upvalues[count].index    = index;
    return c->function->upvalue_count++;
}

static int resolve_upvalue(Compiler *c, Token *name) {
    if (c->enclosing == NULL) return -1;
    int local = resolve_local(c->enclosing, name);
    if (local != -1) {
        c->enclosing->locals[local].is_captured = true;
        return add_upvalue(c, (uint8_t)local, true);
    }
    int upvalue = resolve_upvalue(c->enclosing, name);
    if (upvalue != -1) return add_upvalue(c, (uint8_t)upvalue, false);
    return -1;
}

static void add_local(Token name) {
    if (current->local_count == UINT8_COUNT) {
        error("Too many local variables in function.");
        return;
    }
    Local *l      = &current->locals[current->local_count++];
    l->name       = name;
    l->depth      = -1; // not yet defined
    l->is_captured = false;
}

static void declare_variable(void) {
    if (current->scope_depth == 0) return; // global — handled differently
    Token *name = &parser.previous;
    // Check for duplicate in same scope
    for (int i = current->local_count - 1; i >= 0; i--) {
        Local *l = &current->locals[i];
        if (l->depth != -1 && l->depth < current->scope_depth) break;
        if (identifiers_equal(name, &l->name))
            error("Already a variable with this name in this scope.");
    }
    add_local(*name);
}

static uint8_t identifier_constant(Token *name) {
    return make_constant(OBJ_VAL(obj_string_copy(name->start, name->length)));
}

static uint8_t parse_variable(const char *error_msg) {
    consume(TOKEN_IDENTIFIER, error_msg);
    declare_variable();
    if (current->scope_depth > 0) return 0; // local — slot determined later
    return identifier_constant(&parser.previous);
}

static void mark_initialized(void) {
    if (current->scope_depth == 0) return;
    current->locals[current->local_count - 1].depth = current->scope_depth;
}

static void define_variable(uint8_t global) {
    if (current->scope_depth > 0) { mark_initialized(); return; }
    emit_bytes(OP_DEFINE_GLOBAL, global);
}

// ============================================================
// Named variable get/set
// ============================================================

static void named_variable(Token name, bool can_assign) {
    uint8_t get_op, set_op;
    int arg = resolve_local(current, &name);
    if (arg != -1) {
        get_op = OP_GET_LOCAL;
        set_op = OP_SET_LOCAL;
    } else if ((arg = resolve_upvalue(current, &name)) != -1) {
        get_op = OP_GET_UPVALUE;
        set_op = OP_SET_UPVALUE;
    } else {
        arg    = identifier_constant(&name);
        get_op = OP_GET_GLOBAL;
        set_op = OP_SET_GLOBAL;
    }

    if (can_assign && match_tok(TOKEN_EQUAL)) {
        expression();
        emit_bytes(set_op, (uint8_t)arg);
    } else {
        emit_bytes(get_op, (uint8_t)arg);
    }
}

// ============================================================
// Arguments / parameters
// ============================================================

static uint8_t argument_list(void) {
    uint8_t argc = 0;
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            expression();
            if (argc == 255) error("Can't have more than 255 arguments.");
            argc++;
        } while (match_tok(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    return argc;
}

// ============================================================
// Parse functions (Pratt table)
// ============================================================

static void grouping(bool can_assign) {
    (void)can_assign;
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool can_assign) {
    (void)can_assign;
    double value = strtod(parser.previous.start, NULL);
    emit_constant(NUMBER_VAL(value));
}

static void string(bool can_assign) {
    (void)can_assign;
    // +1 / -2 to skip the surrounding quotes
    emit_constant(OBJ_VAL(obj_string_copy(parser.previous.start + 1,
                                           parser.previous.length - 2)));
}

static void variable(bool can_assign) {
    named_variable(parser.previous, can_assign);
}

static void this_(bool can_assign) {
    (void)can_assign;
    if (current_class == NULL) { error("Can't use 'this' outside of a class."); return; }
    variable(false);
}

static void super_(bool can_assign) {
    (void)can_assign;
    if (current_class == NULL)             error("Can't use 'super' outside of a class.");
    else if (!current_class->has_superclass) error("Can't use 'super' in a class with no superclass.");

    consume(TOKEN_DOT, "Expect '.' after 'super'.");
    consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifier_constant(&parser.previous);

    Token this_tok = {TOKEN_THIS, "this", 4, 0};
    named_variable(this_tok, false);

    if (match_tok(TOKEN_LEFT_PAREN)) {
        uint8_t argc = argument_list();
        Token super_tok = {TOKEN_SUPER, "super", 5, 0};
        named_variable(super_tok, false);
        emit_bytes(OP_SUPER_INVOKE, name);
        emit_byte(argc);
    } else {
        Token super_tok = {TOKEN_SUPER, "super", 5, 0};
        named_variable(super_tok, false);
        emit_bytes(OP_GET_SUPER, name);
    }
}

static void literal(bool can_assign) {
    (void)can_assign;
    switch (parser.previous.type) {
        case TOKEN_FALSE: emit_byte(OP_FALSE); break;
        case TOKEN_NIL:   emit_byte(OP_NIL);   break;
        case TOKEN_TRUE:  emit_byte(OP_TRUE);  break;
        default: return;
    }
}

static void unary(bool can_assign) {
    (void)can_assign;
    TokenType op = parser.previous.type;
    parse_precedence(PREC_UNARY);
    switch (op) {
        case TOKEN_BANG:  emit_byte(OP_NOT);    break;
        case TOKEN_MINUS: emit_byte(OP_NEGATE); break;
        default: return;
    }
}

static void binary(bool can_assign) {
    (void)can_assign;
    TokenType op   = parser.previous.type;
    ParseRule *rule = get_rule(op);
    parse_precedence((Precedence)(rule->precedence + 1));
    switch (op) {
        case TOKEN_PLUS:          emit_byte(OP_ADD);           break;
        case TOKEN_MINUS:         emit_byte(OP_SUBTRACT);      break;
        case TOKEN_STAR:          emit_byte(OP_MULTIPLY);      break;
        case TOKEN_SLASH:         emit_byte(OP_DIVIDE);        break;
        case TOKEN_BANG_EQUAL:    emit_byte(OP_NOT_EQUAL);     break;
        case TOKEN_EQUAL_EQUAL:   emit_byte(OP_EQUAL);         break;
        case TOKEN_GREATER:       emit_byte(OP_GREATER);       break;
        case TOKEN_GREATER_EQUAL: emit_byte(OP_GREATER_EQUAL); break;
        case TOKEN_LESS:          emit_byte(OP_LESS);          break;
        case TOKEN_LESS_EQUAL:    emit_byte(OP_LESS_EQUAL);    break;
        default: return;
    }
}

static void and_(bool can_assign) {
    (void)can_assign;
    int end_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    parse_precedence(PREC_AND);
    patch_jump(end_jump);
}

static void or_(bool can_assign) {
    (void)can_assign;
    int else_jump = emit_jump(OP_JUMP_IF_FALSE);
    int end_jump  = emit_jump(OP_JUMP);
    patch_jump(else_jump);
    emit_byte(OP_POP);
    parse_precedence(PREC_OR);
    patch_jump(end_jump);
}

static void call(bool can_assign) {
    (void)can_assign;
    uint8_t argc = argument_list();
    emit_bytes(OP_CALL, argc);
}

static void dot(bool can_assign) {
    consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifier_constant(&parser.previous);

    if (can_assign && match_tok(TOKEN_EQUAL)) {
        expression();
        emit_bytes(OP_SET_PROPERTY, name);
    } else if (match_tok(TOKEN_LEFT_PAREN)) {
        uint8_t argc = argument_list();
        emit_bytes(OP_INVOKE, name);
        emit_byte(argc);
    } else {
        emit_bytes(OP_GET_PROPERTY, name);
    }
}

// ============================================================
// Pratt parse table
// ============================================================

static ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]    = { grouping, call,   PREC_CALL       },
    [TOKEN_RIGHT_PAREN]   = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_LEFT_BRACE]    = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_RIGHT_BRACE]   = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_COMMA]         = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_DOT]           = { NULL,     dot,    PREC_CALL       },
    [TOKEN_MINUS]         = { unary,    binary, PREC_TERM       },
    [TOKEN_PLUS]          = { NULL,     binary, PREC_TERM       },
    [TOKEN_SEMICOLON]     = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_SLASH]         = { NULL,     binary, PREC_FACTOR     },
    [TOKEN_STAR]          = { NULL,     binary, PREC_FACTOR     },
    [TOKEN_BANG]          = { unary,    NULL,   PREC_NONE       },
    [TOKEN_BANG_EQUAL]    = { NULL,     binary, PREC_EQUALITY   },
    [TOKEN_EQUAL]         = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_EQUAL_EQUAL]   = { NULL,     binary, PREC_EQUALITY   },
    [TOKEN_GREATER]       = { NULL,     binary, PREC_COMPARISON },
    [TOKEN_GREATER_EQUAL] = { NULL,     binary, PREC_COMPARISON },
    [TOKEN_LESS]          = { NULL,     binary, PREC_COMPARISON },
    [TOKEN_LESS_EQUAL]    = { NULL,     binary, PREC_COMPARISON },
    [TOKEN_IDENTIFIER]    = { variable, NULL,   PREC_NONE       },
    [TOKEN_STRING]        = { string,   NULL,   PREC_NONE       },
    [TOKEN_NUMBER]        = { number,   NULL,   PREC_NONE       },
    [TOKEN_AND]           = { NULL,     and_,   PREC_AND        },
    [TOKEN_CLASS]         = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_ELSE]          = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_FALSE]         = { literal,  NULL,   PREC_NONE       },
    [TOKEN_FOR]           = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_FUN]           = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_IF]            = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_NIL]           = { literal,  NULL,   PREC_NONE       },
    [TOKEN_OR]            = { NULL,     or_,    PREC_OR         },
    [TOKEN_PRINT]         = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_RETURN]        = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_SUPER]         = { super_,   NULL,   PREC_NONE       },
    [TOKEN_THIS]          = { this_,    NULL,   PREC_NONE       },
    [TOKEN_TRUE]          = { literal,  NULL,   PREC_NONE       },
    [TOKEN_VAR]           = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_WHILE]         = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_ERROR]         = { NULL,     NULL,   PREC_NONE       },
    [TOKEN_EOF]           = { NULL,     NULL,   PREC_NONE       },
};

static ParseRule *get_rule(TokenType type) { return &rules[type]; }

static void parse_precedence(Precedence prec) {
    advance_token();
    ParseFn prefix = get_rule(parser.previous.type)->prefix;
    if (prefix == NULL) { error("Expect expression."); return; }

    bool can_assign = prec <= PREC_ASSIGNMENT;
    prefix(can_assign);

    while (prec <= get_rule(parser.current.type)->precedence) {
        advance_token();
        ParseFn infix = get_rule(parser.previous.type)->infix;
        infix(can_assign);
    }

    if (can_assign && match_tok(TOKEN_EQUAL))
        error("Invalid assignment target.");
}

static void expression(void) { parse_precedence(PREC_ASSIGNMENT); }

// ============================================================
// Statements
// ============================================================

static void synchronize(void) {
    parser.panic_mode = false;
    while (parser.current.type != TOKEN_EOF) {
        if (parser.previous.type == TOKEN_SEMICOLON) return;
        switch (parser.current.type) {
            case TOKEN_CLASS: case TOKEN_FUN: case TOKEN_VAR:
            case TOKEN_FOR: case TOKEN_IF: case TOKEN_WHILE:
            case TOKEN_PRINT: case TOKEN_RETURN:
                return;
            default: break;
        }
        advance_token();
    }
}

static void block(void) {
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) declaration();
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
    Compiler c;
    compiler_init(&c, type);
    scope_begin();

    consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
    if (!check(TOKEN_RIGHT_PAREN)) {
        do {
            current->function->arity++;
            if (current->function->arity > 255) error_at_current("Can't have more than 255 parameters.");
            uint8_t constant = parse_variable("Expect parameter name.");
            define_variable(constant);
        } while (match_tok(TOKEN_COMMA));
    }
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(TOKEN_LEFT_BRACE,  "Expect '{' before function body.");
    block();

    ObjFunction *fn = compiler_end();
    emit_bytes(OP_CLOSURE, make_constant(OBJ_VAL(fn)));
    for (int i = 0; i < fn->upvalue_count; i++) {
        emit_byte(c.upvalues[i].is_local ? 1 : 0);
        emit_byte(c.upvalues[i].index);
    }
}

static void method(void) {
    consume(TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifier_constant(&parser.previous);

    FunctionType type = TYPE_METHOD;
    if (parser.previous.length == 4 &&
        memcmp(parser.previous.start, "init", 4) == 0)
        type = TYPE_INITIALIZER;

    function(type);
    emit_bytes(OP_METHOD, constant);
}

static void print_statement(void) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emit_byte(OP_PRINT);
}

static void expression_statement(void) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
    emit_byte(OP_POP);
}

static void if_statement(void) {
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int then_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();

    int else_jump = emit_jump(OP_JUMP);
    patch_jump(then_jump);
    emit_byte(OP_POP);

    if (match_tok(TOKEN_ELSE)) statement();
    patch_jump(else_jump);
}

static void while_statement(void) {
    int loop_start = current_chunk()->count;
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    int exit_jump = emit_jump(OP_JUMP_IF_FALSE);
    emit_byte(OP_POP);
    statement();
    emit_loop(loop_start);

    patch_jump(exit_jump);
    emit_byte(OP_POP);
}

static void for_statement(void) {
    scope_begin();
    consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    // Initializer
    if (match_tok(TOKEN_SEMICOLON)) {
        // nothing
    } else if (match_tok(TOKEN_VAR)) {
        // var decl inside for
        consume(TOKEN_IDENTIFIER, "Expect variable name.");
        uint8_t global = 0;
        declare_variable();
        if (match_tok(TOKEN_EQUAL)) expression(); else emit_byte(OP_NIL);
        consume(TOKEN_SEMICOLON, "Expect ';' after loop initializer.");
        if (current->scope_depth > 0) {
            mark_initialized();
            global = 0;
        } else {
            global = identifier_constant(&parser.previous);
            emit_bytes(OP_DEFINE_GLOBAL, global);
        }
        (void)global;
    } else {
        expression_statement();
    }

    int loop_start = current_chunk()->count;
    int exit_jump  = -1;

    // Condition
    if (!match_tok(TOKEN_SEMICOLON)) {
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
        exit_jump = emit_jump(OP_JUMP_IF_FALSE);
        emit_byte(OP_POP);
    }

    // Increment — compile then jump over; body jumps back here
    if (!match_tok(TOKEN_RIGHT_PAREN)) {
        int body_jump  = emit_jump(OP_JUMP);
        int incr_start = current_chunk()->count;
        expression();
        emit_byte(OP_POP);
        consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
        emit_loop(loop_start);
        loop_start = incr_start;
        patch_jump(body_jump);
    }

    statement();
    emit_loop(loop_start);

    if (exit_jump != -1) {
        patch_jump(exit_jump);
        emit_byte(OP_POP);
    }

    scope_end();
}

static void return_statement(void) {
    if (current->type == TYPE_SCRIPT) error("Can't return from top-level code.");
    if (match_tok(TOKEN_SEMICOLON)) {
        emit_return();
    } else {
        if (current->type == TYPE_INITIALIZER)
            error("Can't return a value from an initializer.");
        expression();
        consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
        emit_byte(OP_RETURN);
    }
}

static void statement(void) {
    if      (match_tok(TOKEN_PRINT))      print_statement();
    else if (match_tok(TOKEN_IF))         if_statement();
    else if (match_tok(TOKEN_WHILE))      while_statement();
    else if (match_tok(TOKEN_FOR))        for_statement();
    else if (match_tok(TOKEN_RETURN))     return_statement();
    else if (match_tok(TOKEN_LEFT_BRACE)) { scope_begin(); block(); scope_end(); }
    else                                  expression_statement();
}

// ============================================================
// Declarations
// ============================================================

static void var_declaration(void) {
    uint8_t global = parse_variable("Expect variable name.");
    if (match_tok(TOKEN_EQUAL)) expression(); else emit_byte(OP_NIL);
    consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
    define_variable(global);
}

static void fun_declaration(void) {
    uint8_t global = parse_variable("Expect function name.");
    mark_initialized(); // allow recursion
    function(TYPE_FUNCTION);
    define_variable(global);
}

static void class_declaration(void) {
    consume(TOKEN_IDENTIFIER, "Expect class name.");
    Token class_name = parser.previous;
    uint8_t name_constant = identifier_constant(&parser.previous);
    declare_variable();

    emit_bytes(OP_CLASS, name_constant);
    define_variable(name_constant);

    ClassCompiler class_compiler;
    class_compiler.has_superclass = false;
    class_compiler.enclosing      = current_class;
    current_class                 = &class_compiler;

    // Inheritance
    if (match_tok(TOKEN_LESS)) {
        consume(TOKEN_IDENTIFIER, "Expect superclass name.");
        variable(false); // push superclass

        if (identifiers_equal(&class_name, &parser.previous))
            error("A class can't inherit from itself.");

        scope_begin();
        Token super_tok = {TOKEN_SUPER, "super", 5, 0};
        add_local(super_tok);
        define_variable(0);

        named_variable(class_name, false);
        emit_byte(OP_INHERIT);
        class_compiler.has_superclass = true;
    }

    named_variable(class_name, false); // push class for OP_METHOD
    consume(TOKEN_LEFT_BRACE,  "Expect '{' before class body.");
    while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) method();
    consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
    emit_byte(OP_POP); // pop class

    if (class_compiler.has_superclass) scope_end();

    current_class = current_class->enclosing;
}

static void declaration(void) {
    if      (match_tok(TOKEN_CLASS)) class_declaration();
    else if (match_tok(TOKEN_FUN))   fun_declaration();
    else if (match_tok(TOKEN_VAR))   var_declaration();
    else                             statement();

    if (parser.panic_mode) synchronize();
}

// ============================================================
// Entry point
// ============================================================

ObjFunction *compile(const char *source) {
    scanner_init(&scanner, source);
    parser.had_error  = false;
    parser.panic_mode = false;

    Compiler c;
    compiler_init(&c, TYPE_SCRIPT);

    advance_token();
    while (!match_tok(TOKEN_EOF)) declaration();

    ObjFunction *fn = compiler_end();
    return parser.had_error ? NULL : fn;
}

void compiler_mark_roots(void) {
    Compiler *c = current;
    while (c != NULL) {
        gc_mark_object((Obj *)c->function);
        c = c->enclosing;
    }
}
