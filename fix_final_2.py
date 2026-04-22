import re
import os

with open('src/vm.c', 'r') as f:
    vm = f.readlines()

new_vm = []
skip = False
for line in vm:
    if '/* ---- GUI built-ins (same as in interpreter.c) ---- */' in line:
        skip = True
        continue
    if '/* ================================================================== XGUI builtins (VM) */' in line:
        skip = False
        new_vm.append(line)
        continue
    if skip:
        continue

    # Remove references in vm_free
    if 'if (g_vmgui.body) {' in line:
        skip_block = 6 # skip the if block
        continue
    if 'skip_block' in locals() and skip_block > 0:
        skip_block -= 1
        continue

    new_vm.append(line)

with open('src/vm.c', 'w') as f:
    f.writelines(new_vm)

# Ensure no more references in vm_register_builtins
with open('src/vm.c', 'r') as f:
    c = f.read()
c = re.sub(r'\{"gui_window",\s+vmbi_gui_window\},', '', c)
c = re.sub(r'\{"gui_label",\s+vmbi_gui_label\},', '', c)
c = re.sub(r'\{"gui_button",\s+vmbi_gui_button\},', '', c)
c = re.sub(r'\{"gui_input",\s+vmbi_gui_input\},', '', c)
c = re.sub(r'\{"gui_run",\s+vmbi_gui_run\},', '', c)
with open('src/vm.c', 'w') as f:
    f.write(c)
