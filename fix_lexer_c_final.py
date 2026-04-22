import re

with open('src/lexer.c', 'r') as f:
    c = f.read()

# Keywords
if '{"range",    TOKEN_RANGE_KW}' not in c:
    c = c.replace('{"step",     TOKEN_STEP},', '{"step",     TOKEN_STEP},\n    {"range",    TOKEN_RANGE_KW},')

# token_type_name
if 'case TOKEN_RANGE_KW:   return "range";' not in c:
    c = c.replace('case TOKEN_STEP:       return "step";', 'case TOKEN_STEP:       return "step";\n        case TOKEN_RANGE_KW:   return "range";')

with open('src/lexer.c', 'w') as f:
    f.write(c)
