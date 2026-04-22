import re

with open('src/vm.c', 'r') as f:
    content = f.read()

# Completely replace the corrupted area around vm_free
start_marker = 'void vm_free(VM *vm) {'
end_marker = '/* ================================================================== method dispatch hash table'

clean_vm_free = r'''void vm_free(VM *vm) {
    if (!vm) return;
    if (vm->jit) {
        if (vm->jit_verbose) jit_print_stats(vm->jit);
        jit_free(vm->jit);
        vm->jit = (JIT*)TO_NULL();
    }
    gc_collect_audit(vm->gc, vm->globals, vm, NULL);
    env_free(vm->globals);
#ifdef HAVE_X11
    if (g_vm_xgui) { xgui_destroy(g_vm_xgui); g_vm_xgui = NULL; }
#endif
    free(vm);
}

'''

start_idx = content.find(start_marker)
end_idx = content.find(end_marker)

if start_idx != -1 and end_idx != -1:
    new_content = content[:start_idx] + clean_vm_free + content[end_idx:]
    with open('src/vm.c', 'w') as f:
        f.write(new_content)
else:
    print(f"Markers not found: start={start_idx}, end={end_idx}")
