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

#define DEFAULT_PADDING  2
#define MIN_SEL_SCALE  0.01
#define COPY_SEL_MAPPING 2

void get_selection_context(int selection_type, struct SelectionContext *sc);
void free_selection_context(struct SelectionContext *sc);
void get_selection_context_rect(struct SelectionContext *sc);
void get_selection_context_lasso(struct SelectionContext *sc);
gboolean item_within_selection(struct Item *item, struct SelectionContext *sc);
void get_new_selection(int selection_type, struct Layer *layer);
void start_selectregion(GdkEvent *event);
void finalize_selectregion(void);
void start_selectrect(GdkEvent *event);
void finalize_selectrect(void);
void populate_selection(struct SelectionContext *sc);
void select_object_maybe(struct SelectionContext *sc);
void render_selection_marquee(struct SelectionContext *sc);
void finalize_selection(int selection_type);
gboolean start_movesel(GdkEvent *event);
void start_vertspace(GdkEvent *event);
void continue_movesel(GdkEvent *event);
void continue_copysel(GdkEvent *event);
void finalize_movesel(void);
void finalize_copysel(void);
gboolean start_resizesel(GdkEvent *event);
void continue_resizesel(GdkEvent *event);
void finalize_resizesel(void);

void selection_delete(void);

/* clipboard-related functions */

void selection_to_clip(void);
void clipboard_paste_get_offset(double *hoffset, double *voffset);
void clipboard_paste_with_offset(gboolean use_provided_offset, double hoffset, double voffset);
void clipboard_paste(void);
int buffer_size_for_item(struct Item *item);
int buffer_size_for_header(int nimages);
void put_item_in_buffer(struct Item *item, char *p);
void get_item_from_buffer(struct Item *item, unsigned char *p, double hoffset, double voffset);
void import_img_as_clipped_item();

void recolor_selection(int color_no, guint color_rgba);
void rethicken_selection(int val);

/* text functions */

#define DEFAULT_FONT "Sans"
#define DEFAULT_FONT_SIZE 12

void start_text(GdkEvent *event, struct Item *item);
void end_text(void);
void update_text_item_displayfont(struct Item *item);
void rescale_text_items(void);
gboolean item_under_point(struct Item *item, double x, double y);
struct Item *click_is_in_object(struct Layer *layer, double x, double y);
struct Item *click_is_in_text(struct Layer *layer, double x, double y);
void refont_text_item(struct Item *item, gchar *font_name, double font_size);
void process_font_sel(gchar *str);


/* image functions */

void insert_image(GdkEvent *event, struct Item *item);
