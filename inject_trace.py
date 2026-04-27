import re

filepath = 'src/vm.c'
with open(filepath, 'r') as f:
    content = f.read()

trace_code = """
                          if (getenv("PRISM_TRACE")) {
                            printf("[%d] OP %d | Stack (%d): ", frame->ip-1, _op, vm->stack_top);
                            for(int _si=0; _si<vm->stack_top; _si++) { value_print(vm->stack[_si]); printf(" "); }
                            printf("\\n");
                          }
"""

content = content.replace('                          gc_set_alloc_site(frame->chunk->source_file, line);',
                          '                          gc_set_alloc_site(frame->chunk->source_file, line);' + trace_code)

with open(filepath, 'w') as f:
    f.write(content)
