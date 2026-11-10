#include <gtk/gtk.h>

// Callback function to show the message dialog
static void on_button_clicked(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog;

    // Create a dialog window with a "Hello" title
    dialog = gtk_message_dialog_new(GTK_WINDOW(data),
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_OK,
                                    "Hello World!");

    gtk_window_set_title(GTK_WINDOW(dialog), "Hello, world");

    // Run the dialog (this will block until the user closes it)
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *button;
    GtkWidget *box;

    // Initialize GTK
    gtk_init(&argc, &argv);

    // Create a window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    // By default in the Misskey extended API there is no function
    // to set the program title name, so this will have to do
    // We can add a extended function like Mk:SetTitle("title") to allow changing it
    gtk_window_set_title(GTK_WINDOW(window), "AiScript UI window");
    gtk_window_set_default_size(GTK_WINDOW(window), 300, 200);

    // Connect the "destroy" signal of the window to terminate the application
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Create a small button with the text "Click to say hello"
    button = gtk_button_new_with_label("Click to say hello");

    // Connect the button's "clicked" signal to the callback function
    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), window);

    // Create a box container
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);  // 0 space between elements
    gtk_container_add(GTK_CONTAINER(window), box);

    // Use GTK alignment to center the button in the middle of the window
    GtkWidget *alignment = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(alignment), button);

    // Add the alignment widget to the box
    gtk_box_pack_start(GTK_BOX(box), alignment, TRUE, TRUE, 0);

    // Center the window on the screen
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    // Show all widgets in the window
    gtk_widget_show_all(window);

    // Start the GTK main loop
    gtk_main();

    return 0;
}

