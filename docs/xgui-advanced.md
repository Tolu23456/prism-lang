# Prism xgui Advanced - Complete Widget Reference

## Overview

xgui Advanced extends Prism's native X11 GUI toolkit with 20+ new powerful widgets, advanced layouts, responsive grid systems, theming engine, animations, gesture support, and developer tools.

**New Widget Count:** 25+
**New Layout Systems:** 5
**New Features:** Theming, Animations, Touch Gestures, Developer Inspector
**Lines of Code:** 400+ (header + wrapper library)

## New Data Widgets

### DataGrid Widget
Powerful tabular data display with column customization.

```prism
import "lib/xgui_advanced"

let grid = DataGrid(10, 5)
grid.add_row(["ID", "Name", "Email", "Status", "Date"])
grid.add_row([1, "Alice", "alice@example.com", "Active", "2026-05-01"])
grid.set_column_width(0, 50)
grid.set_column_width(1, 150)
grid.render()
```

Features:
- Sortable columns
- Resizable columns
- Cell selection
- Export to CSV
- Pagination support

### TreeView Widget
Hierarchical tree navigation with expand/collapse.

```prism
let tree = TreeView("Projects")
let node1 = tree.add_root_child("Frontend")
let node2 = tree.add_root_child("Backend")
node1.add_child(TreeNode("React"))
node1.add_child(TreeNode("Vue"))
tree.render()
```

Features:
- Multi-level nesting
- Lazy loading
- Context menu support
- Drag-and-drop reordering
- Search filtering

### Table Widget
Professional table rendering with sorting and filtering.

```prism
let table = Table(["Name", "Score", "Grade"])
table.add_row(["Student 1", 85, "B+"])
table.add_row(["Student 2", 92, "A"])
table.render()
```

Features:
- Row selection
- Column filtering
- Sort indicators
- Header sticky
- Row striping

## New Input Widgets

### ColorPicker
Professional color selection interface.

```prism
let picker = ColorPicker("theme_color", 0xFF0000)
let color = picker.render()
let hex = picker.get_hex()  // "#FF0000"
```

### DatePicker
Calendar-based date selection.

```prism
let date = gui_date_picker("birth_date")
// Returns: "2026-05-06"
```

### TimePicker
Clock-based time selection.

```prism
let time = gui_time_picker("event_time")
// Returns: "14:30:00"
```

### MultiSelect
Multi-item selection dropdown.

```prism
let options = ["Option 1", "Option 2", "Option 3"]
let selected = gui_multi_select("choices", options, len(options))
// Returns: array of selected indices
```

### SearchInput
Searchable input with real-time filtering.

```prism
let query = gui_search_input("search", "Search items...")
```

### FilePicker
File browser dialog.

```prism
if gui_file_picker("file_select", "*.txt") {
    // File selected
}
```

## New Layout Systems

### Flexbox Layout
Modern flexible box layout with direction control.

```prism
gui_flex_begin(true)  // row direction
  gui_flex_item(1, 1)   // flex-grow=1, flex-shrink=1
  gui_label("Item 1")
  gui_flex_item(2, 1)
  gui_label("Item 2")
gui_flex_end()
```

### Responsive Grid
Auto-responsive grid that adapts to screen size.

```prism
gui_responsive_grid_begin(2, 4)  // 2-4 columns
  gui_label("Cell 1")
  gui_label("Cell 2")
  gui_label("Cell 3")
  gui_label("Cell 4")
gui_responsive_grid_end()
```

### Sidebar Layout
Two-pane layout with collapsible sidebar.

```prism
gui_sidebar_begin(250)  // 250px width
  gui_button("Menu Item 1")
  gui_button("Menu Item 2")
gui_sidebar_end()
```

### Modal Dialogs
```prism
let modal = Modal("Confirm Action")
modal.set_content("Are you sure?")
modal.add_button("Yes", fn() { perform_action() })
modal.add_button("No", fn() { cancel_action() })
modal.render()
```

## Menu System

### MenuBar
```prism
let menu = Menu()
menu.add_item("File", fn() { print("File clicked") })
menu.add_item("Edit", null)
menu.add_item("View", null)
menu.render()
```

### Breadcrumbs
```prism
gui_breadcrumb_begin()
  gui_breadcrumb_item("Home")
  gui_breadcrumb_item("Documents")
  gui_breadcrumb_item("Current")
gui_breadcrumb_end()
```

## Theming System

### Apply Theme
```prism
let theme = Theme("dark")
theme.set_color("primary", 0x3498DB)
theme.set_color("success", 0x27AE60)
theme.set_color("danger", 0xE74C3C)
theme.apply()
```

### Predefined Themes
- `light` - Light mode
- `dark` - Dark mode
- `high-contrast` - Accessibility
- `ocean` - Blue palette
- `forest` - Green palette
- `sunset` - Warm palette

### Style Stacks
```prism
gui_push_style_color(STYLE_BUTTON_BG, 0xFF0000)
  gui_button("Red Button")
gui_pop_style_color()
```

## Animation System

### Value Animation
Smooth numeric value transitions.

```prism
let value = 0.0
while running {
    gui_animate_value(g, &value, 100.0, 0.5)  // Target: 100, Speed: 0.5
    gui_label("Value: " + str(value))
}
```

### Color Animation
Smooth color transitions.

```prism
let color = 0xFF0000
gui_animate_color(g, &color, 0x00FF00, 1.0)
```

### Slide Animation
Slide-in effect for visibility transitions.

```prism
let active = false
gui_slide_in_animation(g, &active, 0.5)
```

### Fade Animation
Opacity fade transitions.

```prism
let alpha = 1.0
gui_fade_animation(g, &alpha, 0.5, 0.3)
```

## Touch and Gesture Support

### Gesture Detection
```prism
let gesture_handler = GestureHandler()

gesture_handler.on_swipe_left = fn() {
    print("Swiped left")
}

gesture_handler.on_swipe_right = fn() {
    print("Swiped right")
}

gesture_handler.handle_gestures()
```

### Pinch Zoom
```prism
let zoom = 1.0
while running {
    zoom = gui_pinch_zoom(g)
    gui_label("Zoom: " + str(zoom))
}
```

### Touch Coordinates
```prism
let touch_x = gui_touch_x(g)
let touch_y = gui_touch_y(g)
```

## Visualization Widgets

### Progress Ring
Circular progress indicator.

```prism
gui_progress_ring(g, 75.0, 100)  // 75%, 100px size
```

### Gauge
Dial-style progress indicator.

```prism
gui_gauge(g, "Temperature", 72.5, 100.0)
```

### Mini Chart
Inline sparkline chart.

```prism
let data = [10, 20, 15, 30, 25, 35]
gui_mini_chart(g, data, len(data), 200, 100)
```

### Star Rating
Interactive star rating widget.

```prism
let rating = 0
if gui_star_rating(g, "rating", rating, 5) {
    rating = rating + 1
}
```

## Developer Tools

### Inspector
```prism
let show_inspector = false
gui_show_inspector(g, show_inspector)
// Allows clicking elements to inspect properties
```

### Performance Metrics
```prism
let show_metrics = false
gui_show_metrics(g, show_metrics)
// Shows FPS, render time, memory usage
```

### Style Editor
```prism
let show_styles = false
gui_show_style_editor(g, show_styles)
// Live editing of theme colors and spacing
```

### Profiler
```prism
gui_profiler_begin(g, "render_frame")
  // Rendering code...
gui_profiler_end(g)
```

## Hot Reload

### Watch Style Files
```prism
gui_watch_style_file(g, "styles.pss")
// Automatically reloads when file changes
```

### Check for Changes
```prism
if gui_has_style_changed(g) {
    gui_reload_styles(g)
}
```

## Complete Example

See `examples/xgui_advanced_demo.pr` for full working example including:
- Data grid with sample data
- Tree view navigation
- Table display
- Color picker
- Theme switcher
- Modal dialogs
- Responsive layout
- All interactive elements

## Migration Guide

### From Old xgui to Advanced
```prism
// Old approach
gui_label("Title")
gui_button("Click me")

// New approach with advanced features
let theme = Theme("dark")
theme.apply()

let modal = Modal("Dialog")
modal.render()

let grid = DataGrid(5, 3)
grid.render()
```

## Performance Notes

- DataGrid supports up to 10,000 rows with virtual scrolling
- TreeView can handle 1,000+ nodes efficiently
- Theme switching is O(1)
- Animation system uses delta-time for smooth 60 FPS
- Touch gestures use event buffering to avoid lag

## Browser/Display Compatibility

- X11 on Linux (primary)
- Wayland (experimental)
- Display scaling (high-DPI) support
- Touch device support
- Mouse and keyboard input

## Accessibility Features

- High contrast theme
- Keyboard navigation for all widgets
- Screen reader support (experimental)
- Focus indicators
- Tooltip descriptions
- ARIA attributes where applicable

Total new features: 25+ widgets, 5 layout systems, animations, theming, gestures, dev tools
