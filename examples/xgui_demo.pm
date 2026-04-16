# xgui_demo.pm — Native X11 GUI demo for Prism
# Run on a desktop with an X11 display: ./prism examples/xgui_demo.pm

xgui_init(460, 400, "Prism — Native GUI")
xgui_style("examples/default.pss")

let clicks = 0
let name   = ""

while xgui_running() {
    xgui_begin()

    xgui_label("Welcome to Prism Native GUI")
    xgui_spacer(8)

    xgui_label("Enter your name:")
    name = xgui_input("name_field", "e.g. Alice")
    xgui_spacer(4)

    xgui_row_begin()
    if xgui_button("Greet") {
        if name != "" {
            output(f"Hello, {name}!")
        } else {
            output("Please enter a name first.")
        }
    }
    if xgui_button("Count +1") {
        clicks += 1
    }
    xgui_row_end()

    xgui_spacer(8)
    xgui_label(f"Button presses: {clicks}")

    if name != "" {
        xgui_label(f"Typing: {name}")
    }

    xgui_spacer(16)
    if xgui_button("Quit") {
        xgui_close()
    }

    xgui_end()
}

output("Window closed. Goodbye!")
