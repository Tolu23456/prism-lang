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

# Find the start of the first parse_range and the end of the last parse_range (in case of duplication)
# Then replace with a single clean version.
lines = c.split('\n')
start_idx = -1
end_idx = -1
for i, line in enumerate(lines):
    if 'static ASTNode *parse_range(Parser *p) {' in line:
        if start_idx == -1: start_idx = i
    if line == '}' and i > start_idx and start_idx != -1:
        # Check if next line is also a corrupted part or start of another func
        if i+1 < len(lines) and 'ASTNode *left = parse_logical_or(p);' in lines[i+1]:
             continue
        end_idx = i
        # Keep looking to find the last '}' of the duplicated mess

# Actually, the mess is more complex. Let's just find the markers I used before.
# Or just use re.sub on the whole block between parse_logical_or and parse_nullcoal.

start_marker = '/* ---- range: expr .. expr (step expr)? ---- */'
end_marker = '/* ---- null coalescing: expr ?? expr ---- */'
s = c.find(start_marker)
e = c.find(end_marker)
if s != -1 and e != -1:
    new_c = c[:s] + start_marker + "\n" + new_range_parser + "\n" + c[e:]
    with open('src/parser.c', 'w') as f:
        f.write(new_c)
