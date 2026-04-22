import re
import os

# 1. Fix src/lexer.h - remove duplicate TOKEN_STEP
with open('src/lexer.h', 'r') as f:
    lh = f.read()
lh = lh.replace('TOKEN_SELF,\n    TOKEN_STEP,\n    TOKEN_RANGE_KW,', 'TOKEN_SELF,\n    TOKEN_RANGE_KW,')
with open('src/lexer.h', 'w') as f:
    f.write(lh)

# 2. Fix src/vm.c - remove corrupted GUI code
with open('src/vm.c', 'r') as f:
    vm = f.read()

# Remove the VmGuiState struct and functions
vm = re.sub(r'typedef struct \{.*?VmGuiState;.*?static void vmgui_append.*?static Value vmbi_gui_run.*?\}', '', vm, flags=re.DOTALL)
# It seems re.sub might have failed due to the corruption. Let's try simpler.
# Find start of GUI stuff and end
start_gui = vm.find('typedef struct {')
if start_gui != -1 and 'VmGuiState' in vm[start_gui:start_gui+100]:
    # find where XGUI builtins begin
    end_gui = vm.find('/* ================================================================== XGUI builtins (VM) */')
    if end_gui != -1:
        vm = vm[:start_gui] + vm[end_gui:]

# Ensure no references to deleted functions in the registration table
vm = re.sub(r'\{"gui_window",\s+vmbi_gui_window\},', '', vm)
vm = re.sub(r'\{"gui_label",\s+vmbi_gui_label\},', '', vm)
vm = re.sub(r'\{"gui_button",\s+vmbi_gui_button\},', '', vm)
vm = re.sub(r'\{"gui_input",\s+vmbi_gui_input\},', '', vm)
vm = re.sub(r'\{"gui_run",\s+vmbi_gui_run\},', '', vm)

with open('src/vm.c', 'w') as f:
    f.write(vm)

# 3. Fix src/interpreter.c - ensure it's valid
# I'll use the version I wrote in the previous turn but make sure it has the process_fstring
# actually the version in the previous turn was pretty clean.
