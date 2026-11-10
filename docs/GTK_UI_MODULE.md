# GTK UI Module for AiScript C

A bare-bones native module demonstrating GTK 3 integration with AiScript, providing a Misskey-compatible UI API.

## Overview

This module enables AiScript scripts to create graphical user interfaces using GTK 3, making it possible to run Misskey UI scripts on the C implementation.

## Installation

### Prerequisites

- GTK 3 development libraries
- X11 or Wayland display server
- DISPLAY environment variable set

```bash
# Ubuntu/Debian
sudo apt-get install libgtk-3-dev

# Fedora/RHEL
sudo dnf install gtk3-devel

# Arch Linux
sudo pacman -S gtk3
```

### Building

```bash
cd examples/native
make -f Makefile.ui
cp ui_gtk.so ../
```

## API Reference

### Mk:dialog(title, text, type?)

Display a message dialog.

**Parameters:**
- `title` (string): Dialog window title
- `text` (string): Dialog message content
- `type` (string, optional): Dialog type - `'info'`, `'success'`, `'warning'`, `'error'`, or `'question'`. Defaults to `'info'`.

**Example:**
```aiscript
Mk:dialog("Hello", "World!", "info")
```

### Ui:render(components)

Render components to the main window.

**Parameters:**
- `components` (array): Array of component definition objects

**Example:**
```aiscript
Ui:render([
    Ui:C:text({ text: "Hello, World!" })
])
```

### Ui:C:container(props, id?)

Create a container component for layout.

**Properties:**
- `children` (array): Array of child components
- `align` (string, optional): Horizontal alignment - `'left'`, `'center'`, or `'right'`. Defaults to `'left'`.

**Parameters:**
- `props` (object): Component properties
- `id` (string, optional): Component ID for `Ui:get()`

**Example:**
```aiscript
Ui:C:container({
    align: 'center'
    children: [
        Ui:C:text({ text: "Centered content" })
    ]
})
```

### Ui:C:button(props, id?)

Create a button component.

**Properties:**
- `text` (string): Button label
- `onClick` (function): Callback function when clicked
- `primary` (boolean, optional): Use primary styling. Defaults to `false`.
- `rounded` (boolean, optional): Use rounded corners. Defaults to `false`.
- `disabled` (boolean, optional): Disable the button. Defaults to `false`.

**Parameters:**
- `props` (object): Component properties
- `id` (string, optional): Component ID for `Ui:get()`

**Example:**
```aiscript
Ui:C:button({
    text: "Click me!"
    primary: true
    onClick: @() {
        print("Button clicked!")
    }
})
```

### Ui:C:text(props, id?)

Create a text/label component.

**Properties:**
- `text` (string): Text content to display
- `size` (number, optional): Pango text scale multiplier
- `bold` (boolean, optional): Render text in bold
- `color` (string, optional): Text color, for example `'#fff'`
- `font` (string, optional): Font family name, for example `'monospace'`

**Parameters:**
- `props` (object): Component properties
- `id` (string, optional): Component ID for `Ui:get()`

**Example:**
```aiscript
Ui:C:text({ text: "Hello, World!" })
```

### Ui:C:folder(props, id?)

Create an expandable folder/accordion component.

**Properties:**
- `children` (array): Array of child components
- `title` (string, optional): Expander label text
- `opened` (boolean, optional): Initial expanded state. Defaults to `false`.

**Parameters:**
- `props` (object): Component properties
- `id` (string, optional): Component ID for `Ui:get()`

**Example:**
```aiscript
Ui:C:folder({
    title: "More"
    opened: true
    children: [
        Ui:C:text({ text: "Hidden until opened" })
    ]
})
```

## Complete Example

```aiscript
/// @ 0.16.0

import "ui_gtk.so"

@show() {
    Ui:render([
        Ui:C:container({
            align: 'center'
            children: [
                Ui:C:text({
                    text: "Welcome to AiScript UI!"
                })
                Ui:C:button({
                    text: "Click to say hello"
                    primary: true
                    onClick: @() {
                        Mk:dialog("Hello", "Hello World!", "success")
                    }
                })
            ]
        })
    ])
}

show()
```

## Limitations

This is a **bare-bones demonstration** with the following limitations:

1. **Callback Execution**: Callbacks are simplified and don't have full interpreter context. In a production implementation, you'd need to store the interpreter reference and properly execute AiScript functions.

2. **Component Set**: `container`, `button`, `text`, `folder`, and `Mk:dialog` are implemented. The full Misskey UI API still includes many more components (switch, textInput, numberInput, textarea, select, postForm, etc.).

3. **Display Server**: Requires X11 or Wayland. Won't work in SSH sessions without X forwarding or in environments without a display server.

4. **Main Loop**: The first `Ui:render()` call still enters GTK's blocking main loop, so script execution pauses until the window is closed. Re-renders from callbacks are deferred safely onto GTK's idle loop instead of nesting `gtk_main()`.

5. **Styling**: Limited styling support. `Ui:C:text` supports `size`, `bold`, `color`, and `font`, but many other Misskey UI properties (bgColor, fgColor, borderWidth, etc.) are still not implemented.

6. **Component Registry**: `Ui:get(id)` is not fully implemented - components are registered but update functionality is missing.

## Architecture

### Component System

Components are represented as AiScript objects with a special `__component_type` field:

```aiscript
{
    __component_type: "button"
    props: {
        text: "Click me"
        onClick: @() { ... }
    }
    id: "myButton"  // optional
}
```

### Build Process

1. AiScript component objects are created by `Ui:C:*` functions
2. `Ui:render()` receives an array of component objects
3. Each component is recursively built into GTK widgets
4. GTK widgets are added to the root container
5. The window is shown and GTK main loop starts

### Memory Management

- Component properties use AiScript reference counting
- GTK handles widget lifecycle automatically
- Callbacks are stored with `g_object_set_data_full()` and released automatically

## Extending the Module

To add more components:

1. **Create a builder function** like `build_button()` that converts AiScript props to GTK widgets
2. **Add a case** in `build_component()` to handle the new component type
3. **Create a constructor function** like `ui_c_button()` that creates the component definition object
4. **Register the function** in `as_modinit_ui_gtk()`

Example for adding a checkbox:

```c
// Builder
static GtkWidget* build_checkbox(as_value_t* props) {
    // ... extract props and create GTK checkbox
}

// Constructor
static as_value_t* ui_c_checkbox(as_value_t** args, size_t arg_count, void* userdata) {
    as_value_t* comp = as_value_obj();
    as_obj_set(comp->data.obj_val, "__component_type", as_value_str("checkbox"));
    as_obj_set(comp->data.obj_val, "props", args[0]);
    return comp;
}

// Registration in as_modinit_ui_gtk()
as_value_t* ui_c_checkbox_fn = as_value_fn_native(ui_c_checkbox, NULL);
as_scope_add(module_scope, "Ui:C:checkbox", ui_c_checkbox_fn, false);
as_value_release(ui_c_checkbox_fn);
```

## Future Enhancements

Potential improvements for a production-ready implementation:

1. **Full Interpreter Integration**: Store interpreter context and properly execute callbacks
2. **Complete Component Set**: Implement all Misskey UI components
3. **Non-blocking Event Loop**: Integrate GTK main loop with AiScript execution
4. **Full Styling Support**: Implement all CSS-like styling properties
5. **Component Updates**: Implement `Ui:get(id).update()` functionality
6. **Error Handling**: Proper error propagation and reporting
7. **Thread Safety**: Handle GTK's single-threaded nature with AiScript execution
8. **Headless Mode**: Optional server-side rendering or testing mode
9. **WebView Component**: Embed web content like Misskey's original UI
10. **File Dialogs**: Native file picker dialogs

## Troubleshooting

### "cannot open display"

Make sure:
- You're running in an environment with a display server
- `DISPLAY` environment variable is set (usually `:0`)
- For SSH: use `ssh -X` for X forwarding

### Button clicks don't work

The callback execution is simplified in this demo. Check console output for "Button clicked!" messages. For full callback support, the interpreter context needs to be passed and callbacks need to be properly executed.

### Module not found

Ensure:
- `ui_gtk.so` is in the same directory as your script, or
- `ui_gtk.so` is in a directory listed in `Core:modpath`, or
- You're using the full path: `import "./path/to/ui_gtk.so"`

## License

MIT License - Same as AiScript C implementation

## Credits

- Inspired by Misskey's AiScript UI API
- Built on GTK 3 (LGPL 2.1)
- Part of the AiScript C project


