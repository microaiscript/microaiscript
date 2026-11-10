/*
 * GTK 3 UI Module for AiScript C
 * Bare-bones implementation of Misskey UI API
 * 
 * Provides:
 * - Ui:render(components)
 * - Ui:C:container(props, id?)
 * - Ui:C:button(props, id?)
 * - Ui:C:text(props, id?)
 * - Mk:dialog(title, text, type?)
 */

#include "../../include/aiscript.h"
#include "../../include/std_async.h"
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* GTK must be initialized once */
static gboolean gtk_initialized = FALSE;
static GtkWidget *main_window = NULL;
static GtkWidget *root_container = NULL;
static as_interpreter_t *global_interp = NULL;
static as_value_t *pending_render_components = NULL;
static gboolean render_idle_queued = FALSE;
static guint async_dispatch_source_id = 0;

/* Component storage for Ui:get(id) */
typedef struct ui_component {
    char* id;
    GtkWidget* widget;
    as_value_t* props;
    struct ui_component* next;
} ui_component_t;

static ui_component_t* component_registry = NULL;

/* Forward declarations */
static GtkWidget* build_component(as_value_t* component_def);
static void clear_component_registry(void);
static gboolean apply_render(as_value_t* components);
static gboolean process_pending_render(gpointer data);
static gchar* get_storage_path(void);
static gboolean dispatch_async_callbacks(gpointer data);

/* ============================================================================
 * GTK Initialization
 * ============================================================================ */

static void ensure_gtk_initialized(void) {
    if (!gtk_initialized) {
        int argc = 0;
        char **argv = NULL;
        gtk_init(&argc, &argv);
        gtk_initialized = TRUE;
    }
}

static gchar* get_storage_path(void) {
    const gchar* data_dir = g_get_user_data_dir();
    gchar* module_dir = g_build_filename(data_dir, "aiscript-c", NULL);
    gchar* storage_path = g_build_filename(module_dir, "ui_gtk_storage.ini", NULL);

    g_mkdir_with_parents(module_dir, 0700);
    g_free(module_dir);

    return storage_path;
}

static GKeyFile* load_storage_keyfile(void) {
    GKeyFile* key_file = g_key_file_new();
    gchar* storage_path = get_storage_path();

    g_key_file_load_from_file(key_file, storage_path, G_KEY_FILE_NONE, NULL);
    g_free(storage_path);

    return key_file;
}

static gboolean save_storage_keyfile(GKeyFile* key_file) {
    gboolean ok = FALSE;
    gsize data_len = 0;
    gchar* storage_path = get_storage_path();
    gchar* data = g_key_file_to_data(key_file, &data_len, NULL);

    if (data) {
        ok = g_file_set_contents(storage_path, data, (gssize)data_len, NULL);
        g_free(data);
    }

    g_free(storage_path);
    return ok;
}

static gboolean dispatch_async_callbacks(gpointer data) {
    (void)data;

    if (global_interp) {
        as_std_async_dispatch(global_interp);
    }

    return G_SOURCE_CONTINUE;
}

static void on_main_window_destroy(GtkWidget* widget, gpointer data) {
    (void)widget;
    (void)data;

    main_window = NULL;
    root_container = NULL;
    clear_component_registry();

    if (pending_render_components) {
        as_value_release(pending_render_components);
        pending_render_components = NULL;
    }

    render_idle_queued = FALSE;
    if (async_dispatch_source_id != 0) {
        g_source_remove(async_dispatch_source_id);
        async_dispatch_source_id = 0;
    }

    if (gtk_main_level() > 0) {
        gtk_main_quit();
    }
}

static void ensure_main_window(void) {
    if (!main_window) {
        main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_title(GTK_WINDOW(main_window), "AiScript UI");
        gtk_window_set_default_size(GTK_WINDOW(main_window), 400, 300);
        gtk_window_set_position(GTK_WINDOW(main_window), GTK_WIN_POS_CENTER);
        
        g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_window_destroy), NULL);
        
        /* Create root container */
        root_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_widget_set_margin_start(root_container, 16);
        gtk_widget_set_margin_end(root_container, 16);
        gtk_widget_set_margin_top(root_container, 16);
        gtk_widget_set_margin_bottom(root_container, 16);
        gtk_container_add(GTK_CONTAINER(main_window), root_container);
    }
}

/* ============================================================================
 * Component Registry
 * ============================================================================ */

static void register_component(const char* id, GtkWidget* widget, as_value_t* props) {
    if (!id) return;
    
    ui_component_t* comp = (ui_component_t*)malloc(sizeof(ui_component_t));
    comp->id = g_strdup(id);  /* Use g_strdup instead of strdup */
    comp->widget = widget;
    comp->props = props;
    if (props) as_value_retain(props);
    comp->next = component_registry;
    component_registry = comp;
}

static void clear_component_registry(void) {
    ui_component_t* comp = component_registry;

    while (comp) {
        ui_component_t* next = comp->next;

        g_free(comp->id);
        if (comp->props) {
            as_value_release(comp->props);
        }
        free(comp);

        comp = next;
    }

    component_registry = NULL;
}

/* ============================================================================
 * Mk:dialog - Show message dialog
 * ============================================================================ */

static as_value_t* mk_dialog(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 2) {
        fprintf(stderr, "Mk:dialog requires at least 2 arguments (title, text)\n");
        return as_value_null();
    }
    
    ensure_gtk_initialized();
    ensure_main_window();
    
    const char* title = (args[0]->type == AS_VALUE_STR) ? args[0]->data.str_val : "Dialog";
    const char* text = (args[1]->type == AS_VALUE_STR) ? args[1]->data.str_val : "";
    const char* type = (arg_count >= 3 && args[2]->type == AS_VALUE_STR) ? args[2]->data.str_val : "info";
    GtkWindow* parent = main_window ? GTK_WINDOW(main_window) : NULL;
    
    /* Map type to GTK message type */
    GtkMessageType msg_type = GTK_MESSAGE_INFO;
    if (strcmp(type, "error") == 0) msg_type = GTK_MESSAGE_ERROR;
    else if (strcmp(type, "warning") == 0) msg_type = GTK_MESSAGE_WARNING;
    else if (strcmp(type, "question") == 0) msg_type = GTK_MESSAGE_QUESTION;
    else if (strcmp(type, "success") == 0) msg_type = GTK_MESSAGE_INFO; /* GTK doesn't have success type */
    
    GtkWidget* dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL,
        msg_type,
        GTK_BUTTONS_OK,
        "%s", text
    );
    
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    return as_value_null();
}

static as_value_t* mk_save(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;

    if (arg_count < 2 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Mk:save requires a string key and value\n");
        return as_value_null();
    }

    const char* key = args[0]->data.str_val;
    as_value_t* value = args[1];
    GKeyFile* key_file = load_storage_keyfile();

    switch (value->type) {
        case AS_VALUE_NULL:
            g_key_file_set_string(key_file, "types", key, "null");
            g_key_file_set_string(key_file, "values", key, "");
            break;
        case AS_VALUE_BOOL:
            g_key_file_set_string(key_file, "types", key, "bool");
            g_key_file_set_boolean(key_file, "values", key, value->data.bool_val);
            break;
        case AS_VALUE_NUM:
            g_key_file_set_string(key_file, "types", key, "num");
            g_key_file_set_double(key_file, "values", key, value->data.num_val);
            break;
        case AS_VALUE_STR:
            g_key_file_set_string(key_file, "types", key, "str");
            g_key_file_set_string(key_file, "values", key, value->data.str_val);
            break;
        default:
            fprintf(stderr, "Mk:save currently supports null, bool, num, and str values\n");
            g_key_file_unref(key_file);
            return as_value_null();
    }

    save_storage_keyfile(key_file);
    g_key_file_unref(key_file);

    return as_value_null();
}

static as_value_t* mk_load(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;

    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        fprintf(stderr, "Mk:load requires a string key\n");
        return as_value_null();
    }

    const char* key = args[0]->data.str_val;
    GKeyFile* key_file = load_storage_keyfile();

    if (!g_key_file_has_key(key_file, "types", key, NULL)) {
        g_key_file_unref(key_file);
        return as_value_null();
    }

    gchar* type = g_key_file_get_string(key_file, "types", key, NULL);
    as_value_t* result = as_value_null();

    if (type) {
        if (strcmp(type, "bool") == 0) {
            result = as_value_bool(g_key_file_get_boolean(key_file, "values", key, NULL));
        } else if (strcmp(type, "num") == 0) {
            result = as_value_num(g_key_file_get_double(key_file, "values", key, NULL));
        } else if (strcmp(type, "str") == 0) {
            gchar* stored = g_key_file_get_string(key_file, "values", key, NULL);
            result = as_value_str(stored ? stored : "");
            g_free(stored);
        }
    }

    g_free(type);
    g_key_file_unref(key_file);

    return result;
}

/* ============================================================================
 * Component Builders
 * ============================================================================ */

/* Button click callback */
static void on_button_clicked(GtkWidget* widget, gpointer data) {
    (void)data;
    as_value_t* callback = (as_value_t*)g_object_get_data(G_OBJECT(widget), "aiscript_callback");
    
    if (callback && callback->type == AS_VALUE_FN && global_interp) {
        /* Execute the AiScript callback function */
        as_fn_t* fn = callback->data.fn_val;
        
        if (fn->kind == AS_FN_USER) {
            /* Execute user-defined AiScript function */
            /* Create a scope for the function execution */
            as_scope_t* call_scope = as_scope_create_child(fn->closure_scope ? fn->closure_scope : global_interp->global_scope, "<button_callback>");
            
            /* Execute function body */
            for (size_t i = 0; i < fn->statement_count; i++) {
                as_value_t* result = as_interpreter_eval_node(global_interp, call_scope, fn->statements[i]);
                if (result) {
                    as_value_release(result);
                }
            }
            
            as_scope_release(call_scope);
        } else if (fn->kind == AS_FN_NATIVE) {
            /* Execute native function */
            as_value_t* result = fn->native_fn(NULL, 0, fn->userdata);
            if (result) {
                as_value_release(result);
            }
        }
    }
}

/* Build Ui:C:button */
static GtkWidget* build_button(as_value_t* props) {
    if (props->type != AS_VALUE_OBJ) return NULL;
    
    as_obj_t* obj = props->data.obj_val;
    
    /* Get text property */
    as_value_t* text_val = as_obj_get(obj, "text");
    const char* text = (text_val && text_val->type == AS_VALUE_STR) ? text_val->data.str_val : "Button";
    
    GtkWidget* button = gtk_button_new_with_label(text);
    
    /* Get onClick callback */
    as_value_t* onclick_val = as_obj_get(obj, "onClick");
    if (onclick_val && onclick_val->type == AS_VALUE_FN) {
        /* Store callback for later invocation */
        as_value_retain(onclick_val);
        g_object_set_data_full(G_OBJECT(button), "aiscript_callback", onclick_val, 
            (GDestroyNotify)as_value_release);
        
        /* Connect to clicked signal */
        g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), NULL);
    }
    
    /* Get styling properties */
    as_value_t* primary_val = as_obj_get(obj, "primary");
    if (primary_val && primary_val->type == AS_VALUE_BOOL && primary_val->data.bool_val) {
        /* Add primary style (visual emphasis) */
        GtkStyleContext* context = gtk_widget_get_style_context(button);
        gtk_style_context_add_class(context, "suggested-action");
    }

    as_value_t* disabled_val = as_obj_get(obj, "disabled");
    if (disabled_val && disabled_val->type == AS_VALUE_BOOL && disabled_val->data.bool_val) {
        gtk_widget_set_sensitive(button, FALSE);
    }
    
    return button;
}

/* Build Ui:C:text */
static GtkWidget* build_text(as_value_t* props) {
    if (props->type != AS_VALUE_OBJ) return NULL;
    
    as_obj_t* obj = props->data.obj_val;
    
    /* Get text property */
    as_value_t* text_val = as_obj_get(obj, "text");
    const char* text = (text_val && text_val->type == AS_VALUE_STR) ? text_val->data.str_val : "";
    
    GtkWidget* label = gtk_label_new(text);
    PangoAttrList* attrs = pango_attr_list_new();
    gboolean has_attrs = FALSE;

    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);

    as_value_t* size_val = as_obj_get(obj, "size");
    if (size_val && size_val->type == AS_VALUE_NUM && size_val->data.num_val > 0) {
        PangoAttribute* scale_attr = pango_attr_scale_new(size_val->data.num_val);
        pango_attr_list_insert(attrs, scale_attr);
        has_attrs = TRUE;
    }

    as_value_t* bold_val = as_obj_get(obj, "bold");
    if (bold_val && bold_val->type == AS_VALUE_BOOL && bold_val->data.bool_val) {
        PangoAttribute* weight_attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
        pango_attr_list_insert(attrs, weight_attr);
        has_attrs = TRUE;
    }

    as_value_t* font_val = as_obj_get(obj, "font");
    if (font_val && font_val->type == AS_VALUE_STR) {
        PangoAttribute* family_attr = pango_attr_family_new(font_val->data.str_val);
        pango_attr_list_insert(attrs, family_attr);
        has_attrs = TRUE;
    }

    as_value_t* color_val = as_obj_get(obj, "color");
    if (color_val && color_val->type == AS_VALUE_STR) {
        GdkRGBA color;

        if (gdk_rgba_parse(&color, color_val->data.str_val)) {
            PangoAttribute* color_attr = pango_attr_foreground_new(
                (guint16)(color.red * 65535.0),
                (guint16)(color.green * 65535.0),
                (guint16)(color.blue * 65535.0)
            );
            pango_attr_list_insert(attrs, color_attr);
            has_attrs = TRUE;
        }
    }

    if (has_attrs) {
        gtk_label_set_attributes(GTK_LABEL(label), attrs);
    }

    pango_attr_list_unref(attrs);
    
    return label;
}

/* Build Ui:C:folder */
static GtkWidget* build_folder(as_value_t* props) {
    if (props->type != AS_VALUE_OBJ) return NULL;

    as_obj_t* obj = props->data.obj_val;
    as_value_t* title_val = as_obj_get(obj, "title");
    as_value_t* opened_val = as_obj_get(obj, "opened");
    as_value_t* children_val = as_obj_get(obj, "children");
    const char* title = (title_val && title_val->type == AS_VALUE_STR) ? title_val->data.str_val : "";
    gboolean opened = opened_val && opened_val->type == AS_VALUE_BOOL && opened_val->data.bool_val;
    GtkWidget* expander = gtk_expander_new(title);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    gtk_expander_set_expanded(GTK_EXPANDER(expander), opened);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    if (children_val && children_val->type == AS_VALUE_ARR) {
        as_arr_t* children = children_val->data.arr_val;

        for (size_t i = 0; i < children->length; i++) {
            GtkWidget* child_widget = build_component(children->items[i]);
            if (child_widget) {
                gtk_box_pack_start(GTK_BOX(box), child_widget, FALSE, FALSE, 0);
            }
        }
    }

    gtk_container_add(GTK_CONTAINER(expander), box);

    return expander;
}

/* Build Ui:C:container */
static GtkWidget* build_container(as_value_t* props) {
    if (props->type != AS_VALUE_OBJ) return NULL;
    
    as_obj_t* obj = props->data.obj_val;
    
    /* Create container box */
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    
    /* Get align property */
    as_value_t* align_val = as_obj_get(obj, "align");
    const char* align = (align_val && align_val->type == AS_VALUE_STR) ? align_val->data.str_val : "left";
    
    /* Set box alignment based on align property */
    if (strcmp(align, "center") == 0) {
        gtk_box_set_homogeneous(GTK_BOX(box), FALSE);
        gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    } else if (strcmp(align, "right") == 0) {
        gtk_widget_set_halign(box, GTK_ALIGN_END);
    } else {
        gtk_widget_set_halign(box, GTK_ALIGN_START);
    }
    
    /* Get children array */
    as_value_t* children_val = as_obj_get(obj, "children");
    if (children_val && children_val->type == AS_VALUE_ARR) {
        as_arr_t* children = children_val->data.arr_val;
        
        for (size_t i = 0; i < children->length; i++) {
            GtkWidget* child_widget = build_component(children->items[i]);
            if (child_widget) {
                /* Use GtkAlignment for proper centering (like the working example) */
                if (strcmp(align, "center") == 0) {
                    /* Create alignment widget with center alignment (0.5, 0.5) and no expansion (0, 0) */
                    GtkWidget* alignment = gtk_alignment_new(0.5, 0.5, 0, 0);
                    gtk_container_add(GTK_CONTAINER(alignment), child_widget);
                    /* Pack alignment with expand and fill to take available space */
                    gtk_box_pack_start(GTK_BOX(box), alignment, FALSE, FALSE, 0);
                } else if (strcmp(align, "right") == 0) {
                    /* Right alignment: xalign=1.0 */
                    GtkWidget* alignment = gtk_alignment_new(1.0, 0.5, 0, 0);
                    gtk_container_add(GTK_CONTAINER(alignment), child_widget);
                    gtk_box_pack_start(GTK_BOX(box), alignment, FALSE, FALSE, 0);
                } else {
                    /* Left alignment (default): xalign=0.0 */
                    GtkWidget* alignment = gtk_alignment_new(0.0, 0.5, 0, 0);
                    gtk_container_add(GTK_CONTAINER(alignment), child_widget);
                    gtk_box_pack_start(GTK_BOX(box), alignment, FALSE, FALSE, 0);
                }
            }
        }
    }
    
    return box;
}

/* Build generic component from definition object */
static GtkWidget* build_component(as_value_t* component_def) {
    if (!component_def || component_def->type != AS_VALUE_OBJ) return NULL;
    
    as_obj_t* obj = component_def->data.obj_val;
    
    /* Get component type */
    as_value_t* type_val = as_obj_get(obj, "__component_type");
    if (!type_val || type_val->type != AS_VALUE_STR) return NULL;
    
    const char* type = type_val->data.str_val;
    
    /* Get props */
    as_value_t* props_val = as_obj_get(obj, "props");
    if (!props_val) return NULL;
    
    /* Build based on type */
    GtkWidget* widget = NULL;
    if (strcmp(type, "button") == 0) {
        widget = build_button(props_val);
    } else if (strcmp(type, "text") == 0) {
        widget = build_text(props_val);
    } else if (strcmp(type, "container") == 0) {
        widget = build_container(props_val);
    } else if (strcmp(type, "folder") == 0) {
        widget = build_folder(props_val);
    }
    
    /* Get optional ID */
    as_value_t* id_val = as_obj_get(obj, "id");
    if (id_val && id_val->type == AS_VALUE_STR && widget) {
        register_component(id_val->data.str_val, widget, props_val);
    }
    
    return widget;
}

/* ============================================================================
 * Ui:render - Render components to window
 * ============================================================================ */

static as_value_t* ui_render(as_value_t** args, size_t arg_count, void* userdata) {
    as_interpreter_t* interp = (as_interpreter_t*)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_ARR) {
        fprintf(stderr, "Ui:render requires an array of components\n");
        return as_value_null();
    }
    
    /* Store interpreter globally for callbacks */
    global_interp = interp;
    
    ensure_gtk_initialized();

    if (gtk_main_level() > 0) {
        if (pending_render_components) {
            as_value_release(pending_render_components);
        }

        pending_render_components = args[0];
        as_value_retain(pending_render_components);

        if (!render_idle_queued) {
            render_idle_queued = TRUE;
            g_idle_add(process_pending_render, NULL);
        }

        return as_value_null();
    }

    ensure_main_window();
    if (async_dispatch_source_id == 0) {
        async_dispatch_source_id = g_timeout_add(16, dispatch_async_callbacks, NULL);
    }
    apply_render(args[0]);
    gtk_main();
    
    return as_value_null();
}

static gboolean apply_render(as_value_t* components) {
    if (!components || components->type != AS_VALUE_ARR) return FALSE;

    ensure_main_window();
    clear_component_registry();

    GList* children = gtk_container_get_children(GTK_CONTAINER(root_container));
    for (GList* iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    as_arr_t* component_arr = components->data.arr_val;
    for (size_t i = 0; i < component_arr->length; i++) {
        GtkWidget* widget = build_component(component_arr->items[i]);
        if (widget) {
            gtk_box_pack_start(GTK_BOX(root_container), widget, FALSE, FALSE, 0);
        }
    }

    gtk_widget_show_all(main_window);

    return TRUE;
}

static gboolean process_pending_render(gpointer data) {
    (void)data;

    render_idle_queued = FALSE;

    if (!pending_render_components) {
        return G_SOURCE_REMOVE;
    }

    as_value_t* components = pending_render_components;
    pending_render_components = NULL;

    apply_render(components);
    as_value_release(components);

    return G_SOURCE_REMOVE;
}

/* ============================================================================
 * Ui:C:* Component Constructor Functions
 * ============================================================================ */

static as_value_t* ui_c_button(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        return as_value_null();
    }
    
    /* Create component definition object */
    as_value_t* comp = as_value_obj();
    as_obj_t* comp_obj = comp->data.obj_val;
    
    as_obj_set(comp_obj, "__component_type", as_value_str("button"));
    as_obj_set(comp_obj, "props", args[0]);
    
    /* Optional ID as second argument */
    if (arg_count >= 2 && args[1]->type == AS_VALUE_STR) {
        as_obj_set(comp_obj, "id", args[1]);
    }
    
    return comp;
}

static as_value_t* ui_c_text(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        return as_value_null();
    }
    
    as_value_t* comp = as_value_obj();
    as_obj_t* comp_obj = comp->data.obj_val;
    
    as_obj_set(comp_obj, "__component_type", as_value_str("text"));
    as_obj_set(comp_obj, "props", args[0]);
    
    if (arg_count >= 2 && args[1]->type == AS_VALUE_STR) {
        as_obj_set(comp_obj, "id", args[1]);
    }
    
    return comp;
}

static as_value_t* ui_c_container(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        return as_value_null();
    }
    
    as_value_t* comp = as_value_obj();
    as_obj_t* comp_obj = comp->data.obj_val;
    
    as_obj_set(comp_obj, "__component_type", as_value_str("container"));
    as_obj_set(comp_obj, "props", args[0]);
    
    if (arg_count >= 2 && args[1]->type == AS_VALUE_STR) {
        as_obj_set(comp_obj, "id", args[1]);
    }
    
    return comp;
}

static as_value_t* ui_c_folder(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;

    if (arg_count < 1 || args[0]->type != AS_VALUE_OBJ) {
        return as_value_null();
    }

    as_value_t* comp = as_value_obj();
    as_obj_t* comp_obj = comp->data.obj_val;

    as_obj_set(comp_obj, "__component_type", as_value_str("folder"));
    as_obj_set(comp_obj, "props", args[0]);

    if (arg_count >= 2 && args[1]->type == AS_VALUE_STR) {
        as_obj_set(comp_obj, "id", args[1]);
    }

    return comp;
}

/* ============================================================================
 * Module Initialization
 * ============================================================================ */

AS_EXPORT void as_modinit_ui_gtk(as_interpreter_t* interp, as_scope_t* module_scope) {
    /* Get global scope from interpreter */
    as_scope_t* global = interp->global_scope;
    
    /* Register Mk:dialog */
    as_value_t* mk_dialog_fn = as_value_fn_native(mk_dialog, NULL);
    as_scope_add(global, "Mk:dialog", mk_dialog_fn, false);
    as_scope_add(module_scope, "Mk:dialog", mk_dialog_fn, false);
    as_value_release(mk_dialog_fn);

    as_value_t* mk_save_fn = as_value_fn_native(mk_save, NULL);
    as_scope_add(global, "Mk:save", mk_save_fn, false);
    as_scope_add(module_scope, "Mk:save", mk_save_fn, false);
    as_value_release(mk_save_fn);

    as_value_t* mk_load_fn = as_value_fn_native(mk_load, NULL);
    as_scope_add(global, "Mk:load", mk_load_fn, false);
    as_scope_add(module_scope, "Mk:load", mk_load_fn, false);
    as_value_release(mk_load_fn);
    
    /* Create component function values (reused for both registration methods) */
    as_value_t* ui_c_button_fn = as_value_fn_native(ui_c_button, NULL);
    as_value_t* ui_c_text_fn = as_value_fn_native(ui_c_text, NULL);
    as_value_t* ui_c_container_fn = as_value_fn_native(ui_c_container, NULL);
    as_value_t* ui_c_folder_fn = as_value_fn_native(ui_c_folder, NULL);
    
    /* METHOD 1: Register as double-colon functions (Ui:C:button style) */
    as_scope_add(global, "Ui:C:button", ui_c_button_fn, false);
    as_scope_add(module_scope, "Ui:C:button", ui_c_button_fn, false);
    
    as_scope_add(global, "Ui:C:text", ui_c_text_fn, false);
    as_scope_add(module_scope, "Ui:C:text", ui_c_text_fn, false);
    
    as_scope_add(global, "Ui:C:container", ui_c_container_fn, false);
    as_scope_add(module_scope, "Ui:C:container", ui_c_container_fn, false);

    as_scope_add(global, "Ui:C:folder", ui_c_folder_fn, false);
    as_scope_add(module_scope, "Ui:C:folder", ui_c_folder_fn, false);
    
    /* METHOD 2: Also create Ui:C object for property access (Ui:C.button style) */
    as_value_t* ui_c_obj = as_value_obj();
    as_obj_t* ui_c = ui_c_obj->data.obj_val;
    
    /* Add same functions as properties (doesn't consume references) */
    as_obj_set(ui_c, "button", ui_c_button_fn);
    as_obj_set(ui_c, "text", ui_c_text_fn);
    as_obj_set(ui_c, "container", ui_c_container_fn);
    as_obj_set(ui_c, "folder", ui_c_folder_fn);
    
    /* Register Ui:C as an object */
    as_scope_add(global, "Ui:C", ui_c_obj, false);
    as_scope_add(module_scope, "Ui:C", ui_c_obj, false);
    as_value_release(ui_c_obj);
    
    /* Now release the function values (they're referenced by both methods) */
    as_value_release(ui_c_button_fn);
    as_value_release(ui_c_text_fn);
    as_value_release(ui_c_container_fn);
    as_value_release(ui_c_folder_fn);
    
    /* Register Ui:render (pass interpreter as userdata) */
    as_value_t* ui_render_fn = as_value_fn_native(ui_render, interp);
    as_scope_add(global, "Ui:render", ui_render_fn, false);
    as_scope_add(module_scope, "Ui:render", ui_render_fn, false);
    as_value_release(ui_render_fn);
}

/* Module metadata */
static as_module_metadata_t meta = {
    .name = "ui_gtk",
    .version = "0.1.0",
    .author = "AiScript C Team",
    .description = "GTK 3 UI module providing Misskey-compatible UI API",
    .license = "MIT",
    .url = "https://github.com/aiscript/aiscript-c"
};

AS_EXPORT as_module_metadata_t* as_modmeta_ui_gtk(void) {
    return &meta;
}

