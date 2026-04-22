import re

def fix_vm():
    with open('src/vm.c', 'r') as f:
        c = f.read()

    # Fix all Value **tmp = ... patterns
    c = re.sub(r'Value\s+\*\s+\*tmp\s*=\s*\(', r'Value *tmp = (', c)

    with open('src/vm.c', 'w') as f:
        f.write(c)

def fix_interp():
    with open('src/interpreter.c', 'r') as f:
        c = f.read()

    # Fix strstr comparison
    c = c.replace('!= TO_UNDEFINED()', '!= NULL')

    # Remove the unused builtin implementations to stop warnings
    # (Optional but keeps it clean)

    with open('src/interpreter.c', 'w') as f:
        f.write(c)

fix_vm()
fix_interp()
