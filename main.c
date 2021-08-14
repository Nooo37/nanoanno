#include <stdbool.h>
#include <gtk/gtk.h>
#include <string.h>

#include "draw.h"

#define TEMP_OUT_FILE "/tmp/nanoanno_output.png"
#define TEMP_IN_FILE "/tmp/nanoanno_input.png"
const int DELTA_MOVE = 20;
const float DELTA_ZOOM = 0.04;
const int BUF_SIZE = 1024;

typedef enum {
    BRUSH,
    ERASER,
    TEXT
} Mode;

// TODO: instead of is_stroking, is_erasing etc
typedef enum {
    BRUSHING,
    ERASING,
    TEXTING,
    IDLE
} Activity;

// global state. I hope that is how one does C
Mode mode = BRUSH;
char *dest = NULL;

// Gtk
GdkDisplay *display;
GdkSeat *seat;
GdkDevice *device;

GtkWidget *window;
GtkWidget *canvas;
GtkDialog *text_dialog;
GtkToggleButton *brush_toggle;
GtkToggleButton *eraser_toggle;
GtkToggleButton *text_toggle;

// pixbufs
GdkPixbuf *pix;
GdkPixbuf *old;
GdkPixbuf *before_action;
int img_width = 0;
int img_height = 0;

// geometry
float scale = 0.1;
int mid_x = 0;
int mid_y = 0;
int offset_x = 0;
int offset_y = 0;

// brush settings
bool is_stroking = false;
bool is_erasing = false;
GdkRGBA color1; // primary color
GdkRGBA color2; // secondary color
int radius = 10;
GList *coords = NULL;

// mouse dragging
bool is_dragging = false;
int dragstart_x = 0;
int dragstart_y = 0;
int offset_old_x = 0;
int offset_old_y = 0;


// text tool
bool is_texting = false;
gchar *text = "";
gchar *font = "Sans";
int text_x = 20;
int text_y = 20;
gint font_size = 12;


// updates the drawing area based on global state
static gboolean update_drawing_area() 
{
    /* cairo_t *cr; */
    GtkAllocation *alloc = g_new(GtkAllocation, 1);
    gtk_widget_get_allocation(canvas, alloc);
    int area_width = alloc->width;
    int area_height = alloc->height;

    // initalize stuff
    /* cr = gdk_cairo_create(gtk_widget_get_window(canvas)); */
    cairo_region_t * cairoRegion = cairo_region_create();
    GdkDrawingContext * drawingContext;
    GdkWindow* window = gtk_widget_get_window(canvas);
    drawingContext = gdk_window_begin_draw_frame (window,cairoRegion);
    cairo_t* cr = gdk_drawing_context_get_cairo_context (drawingContext);

    // "clear" background
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);

    // update geometry
    mid_x = (area_width - img_width * scale) / 2;
    mid_y = (area_height - img_height * scale) / 2;

    // draw
    cairo_translate(cr, mid_x + offset_x, mid_y + offset_y);
    cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, pix, 0, 0);
    cairo_paint(cr);

    // cleanup
    gdk_window_end_draw_frame(window,drawingContext);
    cairo_region_destroy(cairoRegion);
    g_free(alloc);

    return TRUE;
}

// connect to that to get painting (and future dragging) abilities
static gint motion_notify_event( GtkWidget *widget,
                                 GdkEventMotion *event )
{
    int x, y, x_translated, y_translated;
    GdkModifierType state;

    // get coords
    if (event->is_hint) {
        gdk_window_get_device_position (event->window, device, &x, &y, &state);
    } else {
        x = event->x;
        y = event->y;
        state = event->state;
    }

    x_translated = (x - offset_x - mid_x) / scale;
    y_translated = (y - offset_y - mid_y) / scale;

    // strokes drawing
    if ((state & GDK_BUTTON1_MASK && mode == BRUSH) ||
                    ((state & GDK_BUTTON3_MASK && mode == ERASER))) {
        if (is_stroking) {
            coord_t* temp = g_new(coord_t, 1);
            temp->x = x_translated;
            temp->y = y_translated;
            coords = g_list_append(coords, temp);
            pix = draw_line(before_action, coords, &color1, radius);
            update_drawing_area();
        } else {
            is_stroking = true;
            before_action = pix;
            update_drawing_area();
        }
    } else {
        if (is_stroking) {
            pix = draw_line(before_action, coords, &color1, radius);
            g_list_free(coords);
            coords = NULL;
            update_drawing_area();
        }
        is_stroking = false;
    }

    // strokes erase
    if ((state & GDK_BUTTON3_MASK && mode == BRUSH) ||
                    ((state & GDK_BUTTON1_MASK && mode == ERASER))) {
        if (is_erasing) {
            coord_t* temp = g_new(coord_t, 1);
            temp->x = x_translated;
            temp->y = y_translated;
            coords = g_list_append(coords, temp);
            pix = erase_under_line(old, before_action, coords, radius, 1.0);
            update_drawing_area();
        } else {
            is_erasing = true;
            before_action = pix;
            update_drawing_area();
        }
    } else {
        if (is_erasing) {
            pix = erase_under_line(old, before_action, coords, radius, 1.0);
            g_list_free(coords);
            coords = NULL;
            update_drawing_area();
        }
        is_erasing = false;
    }

    // image dragging
    if (state & GDK_BUTTON2_MASK) {
        if (is_dragging) {
            offset_x = offset_old_x - (dragstart_x - x);
            offset_y = offset_old_y - (dragstart_y - y);
            update_drawing_area();
        } else {
            is_dragging = true;
            offset_old_x = offset_x;
            offset_old_y = offset_y;
            dragstart_x = x;
            dragstart_y = y;
        }
    } else {
        is_dragging = false;
    }

    return TRUE;
}

void temporary_text_display()
{
    pix = draw_text(before_action, text, &color1, font, font_size, text_x, text_y);
    update_drawing_area();
}

// the function to connect to, to update the drawing area on resize
static gboolean on_draw (GtkWidget *da, 
                         cairo_t *cr, 
                         gpointer data)
{
    update_drawing_area();
    return FALSE;
}

// increases the scale of the image
static gboolean increase_scale()
{
    scale += DELTA_ZOOM;
    update_drawing_area();
    return FALSE;
}

// decreases the scale of the image
static gboolean decrease_scale()
{
    scale -= DELTA_ZOOM;
    update_drawing_area();
    return FALSE;
}

// increases the x offset
static gboolean increase_offset_x()
{
    offset_x += DELTA_MOVE;
    update_drawing_area();
    return FALSE;
}

// decreases the x offset
static gboolean decrease_offset_x()
{
    offset_x -= DELTA_MOVE;
    update_drawing_area();
    return FALSE;
}

// increases the y offset
static gboolean increase_offset_y()
{
    offset_y += DELTA_MOVE;
    update_drawing_area();
    return FALSE;
}

// decreases the y offset
static gboolean decrease_offset_y()
{
    offset_y -= DELTA_MOVE;
    update_drawing_area();
    return FALSE;
}

static gint button_press_event( GtkWidget      *widget,
                                GdkEventButton *event )
{
    int x, y;

    if (mode == TEXT) {
        // get coords
        x = event->x;
        y = event->y;
        text_x = (x - offset_x - mid_x) / scale;
        text_y = (y - offset_y - mid_y) / scale;

        if (is_texting) {
            temporary_text_display();
        } else {
            is_texting = true;
            before_action = pix;
            gtk_widget_show((GtkWidget*) text_dialog);
        }
    }
    return TRUE;
}

// connect to that to get scale on mouse scrolling
static gboolean mouse_scroll( GtkWidget *widget,
                              GdkEventScroll *event,
                              gpointer data) 
{
    if (event->direction == GDK_SCROLL_UP)
        increase_scale();
    if (event->direction == GDK_SCROLL_DOWN)
        decrease_scale();
    return TRUE;
}

// undo all changes
static void undo_all_changes() 
{
    pix = old;
    offset_x = 0;
    offset_y = 0;
    update_drawing_area();
}

// switch primary and secondary color
static void switch_colors()
{
    GdkRGBA temp;
    temp = color1;
    color1 = color2;
    color2 = temp;
}

static void quit_text()
{
    /* mode = BRUSH; */
    is_texting = false;
    gtk_toggle_button_set_active(brush_toggle, TRUE);
    gtk_toggle_button_set_active(eraser_toggle, FALSE);
    gtk_toggle_button_set_active(text_toggle, FALSE);
    gtk_widget_hide((GtkWidget*) text_dialog);
}

static void quit_text_tool_ok()
{
    quit_text();
    before_action = pix;
    update_drawing_area();
}

static void quit_text_tool_cancel()
{
    quit_text();
    pix = before_action;
    update_drawing_area();
}

// update the primary colors button
static void update_color_primary(GtkButton *button, gpointer user_data)
{
    GtkColorChooser *chooser = (GtkColorChooser*) user_data;
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(chooser), &color1);
}

// update the secondary colors button
static void update_color_secondary(GtkButton *button, gpointer user_data)
{
    GtkColorChooser *chooser = (GtkColorChooser*) user_data;
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(chooser), &color2);
}

// connect to that to get the color button to do its job
static void change_color1(GtkColorButton *color_button)
{
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_button), &color1);
}

// connect to that to get the color button to do its job
static void change_color2(GtkColorButton *color_button)
{
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(color_button), &color2);
}

static void update_on_brush_toggle(GtkToggleButton *brush_toggle, gpointer user_data)
{
    gboolean button_state;
    button_state = gtk_toggle_button_get_active(brush_toggle);

    if (button_state == TRUE) {
        mode = BRUSH;
        gtk_toggle_button_set_active(eraser_toggle, FALSE);
        gtk_toggle_button_set_active(text_toggle, FALSE);
    } else {
        if (mode == BRUSH)
            gtk_toggle_button_set_active(brush_toggle, TRUE);
    }
}

static void update_on_eraser_toggle(GtkToggleButton *eraser_toggle)
{
    gboolean button_state;
    button_state = gtk_toggle_button_get_active(eraser_toggle);

    if (button_state == TRUE) {
        mode = ERASER;
        gtk_toggle_button_set_active(text_toggle, FALSE);
        gtk_toggle_button_set_active(brush_toggle, FALSE);
    } else {
        if (mode == ERASER)
            gtk_toggle_button_set_active(eraser_toggle, TRUE);
    }
}

static void update_on_text_toggle(GtkToggleButton *text_toggle)
{
    gboolean button_state;
    button_state = gtk_toggle_button_get_active(text_toggle);

    if (button_state == TRUE) {
        mode = TEXT;
        gtk_toggle_button_set_active(eraser_toggle, FALSE);
        gtk_toggle_button_set_active(brush_toggle, FALSE);
    } else {
        if (mode == TEXT)
            gtk_toggle_button_set_active(text_toggle, TRUE);
    }
}

static void update_on_text_buffer_change(GtkTextBuffer *textbuffer)
{
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(textbuffer, &start, &end);
    text = gtk_text_buffer_get_text(textbuffer, &start, &end, TRUE);
    temporary_text_display();
}

static void on_font_set(GtkFontButton* button, gpointer user_data)
{
    const char* font_name = pango_font_family_get_name(gtk_font_chooser_get_font_family((GtkFontChooser*) button));
    /* char* font_face = pango_font_face_get_face_name(gtk_font_chooser_get_font_face((GtkFontChooser*) button)); */
    // TODO: support for font faces
    font_size = gtk_font_chooser_get_font_size((GtkFontChooser*) button) / 1000;
    font = font_name;
    temporary_text_display();
}

// connect to that to get the slider to do its job
static void change_radius(GtkAdjustment *adjust)
{
    radius = gtk_adjustment_get_value(adjust);
}


// get sane scaling default based on the image size at app launch
float get_sane_scale() 
{
    // TODO: fix
    return 1;
}

// writes the stdin stream into a png file to process it further
void write_stdin_to_file()
{
    void *content = malloc(BUF_SIZE);

    FILE *fp = fopen(TEMP_IN_FILE, "w");

    if (fp == 0)
        printf("Error: Couldn't open the temp file\n");

    int read;
    while ((read = fread(content, 1, BUF_SIZE, stdin))) {
        fwrite(content, read, 1, fp);
    }
    if (ferror(stdin))
        printf("Error: Couldn't read from stdin\n");

    fclose(fp);
}


// everything that needs to happen for cleanly quitting the app
void quit()
{
    // the current image to stdout
    if (isatty(1) != 1) { // the first 1 refers to stdout
        remove(TEMP_OUT_FILE);
        gdk_pixbuf_save(pix, TEMP_OUT_FILE, "png", NULL, NULL);
        FILE *a = fopen(TEMP_OUT_FILE, "r");
        int n;
        char s[65536];
        while ((n = fread(s, 1, sizeof(s), a))) {
            fwrite(s, 1, n, stdout);
        }
    }
    // quit gtk
    gtk_main_quit();
}

// saves the image
void save()
{
    if (dest == NULL) {
        // TODO: open file picker
    } else {
        gdk_pixbuf_save(pix, dest, "png", NULL, NULL);
    }
}

// loads the image into the application
int load(char* filename)
{
    GError *err = NULL;

    if (filename == NULL) 
        return 1;
    pix = gdk_pixbuf_new_from_file(filename, &err);
    if (err) {
        g_error_free(err);
        return 1;
    }
    old = gdk_pixbuf_copy(pix);
    img_width = gdk_pixbuf_get_width(pix);
    img_height = gdk_pixbuf_get_height(pix);
    return 0;
}

// global keybinds
gboolean my_key_press(GtkWidget *widget,
                      GdkEventKey *event,
                      gpointer user_data) 
{
    if (event->keyval == 'w')
        quit();
    if (event->keyval == 's')
        save();
    if (event->keyval == 'q') {
        save();
        quit();
    }
    if (event->keyval == 'x')
        undo_all_changes();
    // movement, zoom
    if (event->keyval == 'u')
        increase_scale();
    if (event->keyval == 'i')
        decrease_scale();
    if (event->keyval == 'h')
        increase_offset_x();
    if (event->keyval == 'l')
        decrease_offset_x();
    if (event->keyval == 'j')
        decrease_offset_y();
    if (event->keyval == 'k')
        increase_offset_y();
    return FALSE;
}

// build the gtk ui and connects all signals
int build_ui()
{
    GtkBuilder *builder; 
    GtkButton *undo_button;
    GtkButton *color_switch_button, *text_dialog_ok, *text_dialog_cancel;
    GtkColorChooser *color_picker_primary;
    GtkColorChooser *color_picker_secondary;
    GtkTextBuffer *textbuffer;
    GtkAdjustment *radius_scale;
    GtkFontButton *font_button;

    // init devices (for mouse position and clipboard)
    display = gdk_display_get_default();
    seat = gdk_display_get_default_seat(display);
    device = gdk_seat_get_pointer(seat);

    // building and getting all the widgets, connecting signals
    builder = gtk_builder_new();
    gtk_builder_add_from_file (builder, "window.ui", NULL);

    // main window and its callbacks
    window = GTK_WIDGET(gtk_builder_get_object(builder, "window1"));
    gtk_builder_connect_signals(builder, NULL);
    g_signal_connect(G_OBJECT(window), "destroy", 
                    G_CALLBACK(quit), NULL);
    g_signal_connect(G_OBJECT(window), "key-press-event", 
                    G_CALLBACK(my_key_press), NULL);

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK
                    | GDK_KEY_RELEASE_MASK);

    // drawing area and its callbacks
    canvas = GTK_WIDGET(gtk_builder_get_object(builder, "drawing_area"));
    g_signal_connect(G_OBJECT(canvas), "draw", 
                    G_CALLBACK(on_draw), NULL);
    g_signal_connect (G_OBJECT(canvas), "motion_notify_event", 
                    G_CALLBACK(motion_notify_event), NULL);
    g_signal_connect (G_OBJECT(canvas), "button_press_event", 
                    G_CALLBACK(button_press_event), NULL);
    g_signal_connect (G_OBJECT(canvas), "scroll-event", 
                    G_CALLBACK(mouse_scroll), NULL);

    gtk_widget_set_events (canvas, GDK_EXPOSURE_MASK
                       | GDK_LEAVE_NOTIFY_MASK
                       | GDK_BUTTON_PRESS_MASK
                       | GDK_POINTER_MOTION_MASK
                       | GDK_POINTER_MOTION_HINT_MASK
                       | GDK_SCROLL_MASK);

    undo_button = GTK_BUTTON(gtk_builder_get_object(builder, "undo_button"));
    g_signal_connect(G_OBJECT(undo_button), "pressed", 
                    G_CALLBACK(undo_all_changes), NULL);

    // color picker
    color_picker_primary = GTK_COLOR_CHOOSER(gtk_builder_get_object(builder, "color_picker_primary"));
    g_signal_connect(G_OBJECT(color_picker_primary), "color-set", 
                    G_CALLBACK(change_color1), NULL);
    color_picker_secondary = GTK_COLOR_CHOOSER(gtk_builder_get_object(builder, "color_picker_secondary"));
    g_signal_connect(G_OBJECT(color_picker_secondary), "color-set", 
                    G_CALLBACK(change_color2), NULL);
    color_switch_button = GTK_BUTTON(gtk_builder_get_object(builder, "color_switch"));
    g_signal_connect(G_OBJECT(color_switch_button), "pressed", 
                    G_CALLBACK(switch_colors), NULL);
    g_signal_connect(G_OBJECT(color_switch_button), "pressed", 
                    G_CALLBACK(update_color_primary), (gpointer) color_picker_primary);
    g_signal_connect(G_OBJECT(color_switch_button), "pressed", 
                    G_CALLBACK(update_color_secondary), (gpointer) color_picker_secondary);

    gtk_color_chooser_get_rgba(color_picker_primary, &color1);
    gtk_color_chooser_get_rgba(color_picker_secondary, &color2);

    // modi toggle buttons

    brush_toggle = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "toggle_brush"));
    eraser_toggle = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "toggle_eraser"));
    text_toggle = GTK_TOGGLE_BUTTON(gtk_builder_get_object(builder, "toggle_text"));

    g_signal_connect(G_OBJECT(brush_toggle), "toggled", 
                    G_CALLBACK(update_on_brush_toggle), NULL);
    g_signal_connect(G_OBJECT(eraser_toggle), "toggled", 
                    G_CALLBACK(update_on_eraser_toggle), NULL);
    g_signal_connect(G_OBJECT(text_toggle), "toggled", 
                    G_CALLBACK(update_on_text_toggle), NULL);

    gtk_toggle_button_set_active(brush_toggle, TRUE);
    gtk_toggle_button_set_active(eraser_toggle, FALSE);
    gtk_toggle_button_set_active(text_toggle, FALSE);

    // text dialog
    text_dialog = GTK_DIALOG(gtk_builder_get_object(builder, "text_dialog"));
    gtk_window_set_keep_above((GtkWindow*) text_dialog, TRUE);
    text_dialog_ok = GTK_BUTTON(gtk_builder_get_object(builder, "text_dialog_ok"));
    g_signal_connect(G_OBJECT(text_dialog_ok), "pressed", 
                    G_CALLBACK(quit_text_tool_ok), NULL);
    text_dialog_cancel = GTK_BUTTON(gtk_builder_get_object(builder, "text_dialog_cancel"));
    g_signal_connect(G_OBJECT(text_dialog_cancel), "pressed", 
                    G_CALLBACK(quit_text_tool_cancel), NULL);
    textbuffer = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "textbuffer"));
    g_signal_connect(G_OBJECT(textbuffer), "changed", 
                    G_CALLBACK(update_on_text_buffer_change), NULL);
    font_button = GTK_FONT_BUTTON(gtk_builder_get_object(builder, "font_picker"));
    g_signal_connect(G_OBJECT(font_button), "font_set", 
                    G_CALLBACK(on_font_set), NULL);

    // scale
    radius_scale = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "radius_scale"));
    g_signal_connect(G_OBJECT(radius_scale), "value-changed", 
                    G_CALLBACK(change_radius), NULL);


    // start
    gtk_widget_show(window);                
    scale = get_sane_scale();

    return 0;
}

// main
int main(int argc, char *argv[])
{
    char *image_to_edit = NULL; // path to the image

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            i++;
            if (i >= argc) {
                printf("Missing argument for -o\n");
                return 1;
            } else {
                dest = argv[i];
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            // TODO: help text
            printf("PR a help text please\n");
            return 1;
        }
    }

    // initalize the image

    if (argc > 1) { // is the image a command line argument?
        image_to_edit = argv[1];
    }

    if (isatty(0) != 1) { // 0 refers to stdin, is the image coming in a pipe?
        write_stdin_to_file();
        image_to_edit = TEMP_IN_FILE;
    } 

    // TODO: show something when it's started without a file as arg or stdin
    load(image_to_edit);

    gtk_init(&argc, &argv);

    int t = build_ui();

    gtk_main();

    return 0 | t;
}

