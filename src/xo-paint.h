#ifndef XO_PAINT_H
#define XO_PAINT_H

void set_cursor_busy(gboolean busy);
void update_cursor(void);
void update_cursor_for_resize(double *pt);

void create_new_stroke(GdkEvent *event);
void continue_stroke(GdkEvent *event);
void finalize_stroke(void);
void subdivide_cur_path();

void do_eraser(GdkEvent *event, double radius, gboolean whole_strokes);
void finalize_erasure(void);

void do_hand(GdkEvent *event);

void make_dashed(GnomeCanvasItem *item);
void recolor_selection(int color_no, guint color_rgba);
void rethicken_selection(int val);

/* object-related functions (textboxes, images) */

void rescale_objects(void);
void update_scaled_image_display(struct Item *item);
struct Item *click_is_in_object(struct Layer *layer, double x, double y);
gboolean item_under_point(struct Item *item, double x, double y);

/* text functions */

#define DEFAULT_FONT "Sans"
#define DEFAULT_FONT_SIZE 12

void start_text(GdkEvent *event, struct Item *item);
void end_text(void);
void update_text_item_displayfont(struct Item *item);
void rescale_text_items(void);
struct Item *click_is_in_text(struct Layer *layer, double x, double y);
void refont_text_item(struct Item *item, gchar *font_name, double font_size);
void process_font_sel(gchar *str);


#endif  /* XO_PAINT_H */
