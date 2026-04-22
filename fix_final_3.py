import re
import os

with open('src/vm.c', 'r') as f:
    c = f.read()

# Fix the vm_free #ifdef mismatch and remove g_vmgui reference
c = re.sub(r'void vm_free\(VM \*vm\) \{.*?\}', r'''void vm_free(VM *vm) {
    if (vm->jit) {
        if (vm->jit_verbose) jit_print_stats(vm->jit);
        jit_free(vm->jit);
        vm->jit = TO_NULL();
    }
    gc_collect_audit(vm->gc, vm->globals, vm, NULL);
    env_free(vm->globals);
#ifdef HAVE_X11
    if (g_vm_xgui) { xgui_destroy(g_vm_xgui); g_vm_xgui = NULL; }
#endif
    free(vm);
}''', c, flags=re.DOTALL)

with open('src/vm.c', 'w') as f:
    f.write(c)
