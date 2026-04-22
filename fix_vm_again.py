import re

with open('src/vm.c', 'r') as f:
    c = f.read()

# Robustly fix Value **tmp to Value *tmp where it's used as a stack buffer
c = re.sub(r'Value\s+\*\s+\*tmp\s+=\s+\(n\s+<=\s+VM_CALL_STACK_BUF\)\s+\?\s+_stk_buf\d\s+:\s+malloc\(n\s+\*\s+sizeof\(Value\)\);',
           lambda m: m.group(0).replace('**', '*'), c)

# Correctly handle MAKE_DICT
c = re.sub(r'Value\s+\*\s+\*tmp\s+=\s+\(n\*2\s+<=\s+VM_CALL_STACK_BUF\*2\)\s+\?\s+_stk_buf4\s+:\s+malloc\(n\s+\*\s+2\s+\*\s+sizeof\(Value\)\);',
           lambda m: m.group(0).replace('**', '*'), c)

# Fix casts and access
c = c.replace('((Value*)tmp)[i] = POP();', 'tmp[i] = POP();')
c = c.replace('((Value*)tmp)[i]);', 'tmp[i]);')

with open('src/vm.c', 'w') as f:
    f.write(c)
