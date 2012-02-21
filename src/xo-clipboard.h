#ifndef XO_CLIPBOARD_H
#define XO_CLIPBOARD_H

typedef struct PageCopyContext {
  struct Page *pg;
  int bufsz;
  int nitems;
  int *nimages; // array of nimages, one per layer
  ImgSerContext **serialized_images; // array of serialized_images[]; one s.i.[] per layer
  guchar *buf;
} PageCopyContext;

struct Page* duplicate_page();
struct Page* paste_page();
void copy_page();

/* clipboard-related functions */
// Page copy helper functions
struct PageCopyContext *prepare_page_copy_buffers(struct Page *p);
void free_image_ser_buffers_and_pcc(struct PageCopyContext *pcc);
void put_page_in_buffer(struct PageCopyContext *pcc);
void put_layer_metadata_in_buffer(struct Layer *l, guchar **pp);
void put_page_metadata_in_buffer(struct Page *p, guchar **pp);


int get_nitems_of_type(const GList *items_list, int item_type);
int get_nonserialized_data_size(const GList* items_list);
int get_serialized_data_size(ImgSerContext *serialized_images, int nimages);
void serialize_images_in_list(const GList *items_list, ImgSerContext *serialized_images);

void populate_buffer(GList* items_list, struct ImgSerContext *serialized_images, guchar **pp);

void copy_to_buffer_advance_ptr(guchar **to, gpointer from, gsize size);
void copy_from_buffer_advance_ptr(gpointer to, guchar **from, gsize size);
void put_header_in_buffer(int *bufsz, int *nitems, struct BBox *bbox, guchar **pp);
void put_image_data_in_buffer(struct ImgSerContext *isc, guchar **pp);
void populate_buffer_from_global_sel(struct ImgSerContext *serialized_images, guchar **pp);

void selection_to_clip(void);
void clipboard_paste_get_offset(double *hoffset, double *voffset);
void clipboard_paste_with_offset(gboolean use_provided_offset, double hoffset, double voffset);
void clipboard_paste(void);
int buffer_size_for_item(struct Item *item);
int buffer_size_for_header();
int buffer_size_for_serialized_image(ImgSerContext isc);

void put_item_in_buffer(struct Item *item, guchar **pp);
void get_item_from_buffer(struct Item *item, guchar **pp, double hoffset, double voffset);

void callback_clipboard_get(GtkClipboard *clipboard, GtkSelectionData *selection_data,
			    guint info, gpointer user_data);
void callback_clipboard_clear(GtkClipboard *clipboard, gpointer user_data);

#endif  /* XO_CLIPBOARD_H */
