#ifndef XO_IMAGE_H
#define XO_IMAGE_H

#define IMG_INDEX_MAX_SIZE 11

gchar* encode_embedded_image(GdkPixbuf* image);
GdkPixbuf* decode_embedded_image(const gchar *text, gsize text_len);
struct ImgSerContext serialize_image(GdkPixbuf *image);
GdkPixbuf* deserialize_image(struct ImgSerContext buffer_ctxt);
GdkPixbuf* get_image_scaled_maybe(struct Item *item, double scale);
void set_image_path_name(struct Item *item, char *fname_base, int image_id);
void insert_image(GdkEvent *event, struct Item *item);

void import_img_as_clipped_item();

#endif  /* XO_IMAGE_H */
