func create(width, height, title, port) {
    __gui_create(width, height, title, port)
}

func clear(r, g, b) {
    __gui_clear(r, g, b)
}

func pixel(x, y, r, g, b) {
    __gui_set_pixel(x, y, r, g, b)
}

func rect(x, y, w, h, r, g, b) {
    __gui_rect(x, y, w, h, r, g, b)
}

func circle(cx, cy, radius, r, g, b) {
    __gui_circle(cx, cy, radius, r, g, b)
}

func line(x0, y0, x1, y1, r, g, b) {
    __gui_line(x0, y0, x1, y1, r, g, b)
}

func poll() {
    return __gui_poll()
}

func running() {
    return __gui_running()
}

func stop() {
    __gui_stop()
}

func print_url() {
    __gui_print_url()
}
