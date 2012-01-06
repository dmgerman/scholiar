#ifndef XO_SELECTION_H
#define XO_SELECTION_H


#define DEFAULT_PADDING  2
#define MIN_SEL_SCALE  0.01
#define COPY_SEL_MAPPING 2

gboolean item_within_selection(struct Item *item, struct SelectionContext *sc);
void free_selection_context(struct SelectionContext *sc);
void get_selection_context(int selection_type, struct SelectionContext *sc);
void get_selection_context_rect(struct SelectionContext *sc);
void get_selection_context_lasso(struct SelectionContext *sc);
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
void get_possible_resize_direction(double *pt, gboolean *l, gboolean *r, gboolean *t, gboolean *b);
gboolean start_resizesel(GdkEvent *event);
void continue_resizesel(GdkEvent *event);
void finalize_resizesel(void);

void selection_delete(void);
void reset_selection(void);


#endif  /* XO_SELECTION_H */
