import re

with open('src/parser.c', 'r') as f:
    c = f.read()

new_range_parser = r'''
static ASTNode *parse_range(Parser *p) {
    bool has_kw = match(p, TOKEN_RANGE_KW);
    ASTNode *left = parse_logical_or(p);
    if (p->had_error) return left;
    if (check(p, TOKEN_DOTDOT)) {
        int line = p->current->line; advance(p);
        ASTNode *right = parse_logical_or(p);
        ASTNode *step = NULL;
        if (check(p, TOKEN_STEP)) { advance(p); step = parse_logical_or(p); }
        ASTNode *n = ast_node_new(NODE_RANGE, line);
        n->range_lit.start = left; n->range_lit.end = right; n->range_lit.step = step; n->range_lit.inclusive = true;
        return n;
    }
    if (has_kw) {
        int line = left->line;
        ASTNode *n = ast_node_new(NODE_RANGE, line);
        ASTNode *zero = ast_node_new(NODE_INT_LIT, line); zero->int_lit.value = 0;
        n->range_lit.start = zero; n->range_lit.end = left; n->range_lit.step = NULL; n->range_lit.inclusive = false;
        return n;
    }
    return left;
}
'''
c = re.sub(r'static ASTNode \*parse_range\(Parser \*p\) \{.*?\}', new_range_parser, c, flags=re.DOTALL)

with open('src/parser.c', 'w') as f:
    f.write(c)
