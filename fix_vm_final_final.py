import re

with open('src/vm.c', 'r') as f:
    c = f.read()

# Fix Value **tmp to Value *tmp in all MAKE_... opcodes
c = re.sub(r'Value\s+\*\s+\*tmp\s+=\s+\((.*?)\)\s+\?\s+(.*?)\s+:\s+malloc\((.*?)\);',
           r'Value *tmp = (\1) ? \2 : malloc(\3);', c)

# Fix the array usage in those blocks
c = c.replace('((Value*)tmp)[i] = POP();', 'tmp[i] = POP();')
c = c.replace('value_array_push(arr2, tmp[i]);', 'value_array_push(arr2, tmp[i]);')
c = c.replace('value_release(tmp[i]);', 'value_release(tmp[i]);')

with open('src/vm.c', 'w') as f:
    f.write(c)

# Remove print and range from builtins.c and interpreter.c
def remove_builtin(filepath, name):
    with open(filepath, 'r') as f:
        content = f.read()

    # Remove from table
    content = re.sub(r'\{"' + name + r'",\s+.*?\},\n', '', content)
    # The actual implementation might still be there but won't be reachable.
    # To be cleaner, we should remove the impl too, but it's risky with regex.
    # Let's just remove from the registration tables.

    with open(filepath, 'w') as f:
        f.write(content)

remove_builtin('src/builtins.c', 'print')
remove_builtin('src/builtins.c', 'range')
remove_builtin('src/interpreter.c', 'print')
remove_builtin('src/interpreter.c', 'range')
