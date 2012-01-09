#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-misc.h"
#include "xo-paint.h"
#include "xo-image.h"
#include "xo-clipboard.h"
#include "xo-selection.h"

gchar* encode_embedded_image(GdkPixbuf* image)
{
  char *base64_data;
  struct ImgSerContext isc;
  if (!image) return g_strndup("",0); // returns empty string on invalid input
  isc = serialize_image(image);
  base64_data = g_base64_encode(isc.image_data, isc.stream_length);
#ifdef IMAGE_DEBUG
  printf("Converted stream of length %d to base64 str of length %d\n",(int)isc.stream_length,(int)strlen(base64_data));
#endif
  g_free(isc.image_data);
  return base64_data;
}

GdkPixbuf* decode_embedded_image(const gchar *text, gsize text_len)
{
  struct ImgSerContext isc;
  gsize png_buflen;
  GdkPixbuf *image;
  gchar* base64_str = g_malloc(text_len + 1);
  g_memmove(base64_str, text, text_len);
  base64_str[text_len] = '\0';
  isc.image_data = g_base64_decode(base64_str, &png_buflen);
  isc.stream_length = png_buflen;
#ifdef IMAGE_DEBUG
  printf("Decoded base64 str of length %d to stream of length %d\n",(int)strlen(base64_str),(int)isc.stream_length);
#endif
  image = deserialize_image(isc);
  g_free(base64_str);
  g_free(isc.image_data);
  return image;
}

// Returns newly allocated serialized data
struct ImgSerContext serialize_image(GdkPixbuf* image)
{
  struct ImgSerContext isc;
  GError *error = NULL;
  gdk_pixbuf_save_to_buffer(image, (gchar **)&isc.image_data, &isc.stream_length, "png", &error, NULL);
  return isc;
}

// This allocates new memory for the pixbuf
GdkPixbuf* deserialize_image(ImgSerContext isc)
{
  GInputStream *istream;
  GdkPixbuf *pixbuf;
  GError *error = NULL;

  istream = g_memory_input_stream_new_from_data(isc.image_data, isc.stream_length, NULL);
  pixbuf = gdk_pixbuf_new_from_stream(istream, NULL, &error);
  g_input_stream_close(istream, NULL, &error);
  return pixbuf;
}


void set_image_path_name(struct Item *item, char *fname_base, int image_id)
{
  int buflen = strlen(fname_base) + IMG_INDEX_MAX_SIZE * sizeof(char) + 1;
  g_free(item->image_path);
  item->image_path = g_malloc(buflen);
  g_snprintf(item->image_path, buflen, "%s%d", fname_base, image_id);
}

// Returns a pointer to a newly allocated scaled image; if it's close enough
// to native resolution, duplicate instead of scale
GdkPixbuf* get_image_scaled_maybe(struct Item *item, double scale)
{
  double required_height, required_width, native_width, native_height;
  gboolean use_native;
  double pixels_per_canvas_unit = DEFAULT_ZOOM; // bbox of 75 (canvas units) corresponds 
  double native_threshold_pct = 5;
  double native_thr_w, native_thr_h;
  // to 100 pixels.  Note that this relies on DEFAULT_ZOOM being equal to DISPLAY_DPI / 72
  if (! item || ! item->image) return NULL;
  required_height = (item->bbox.bottom-item->bbox.top) * scale;
  required_width = (item->bbox.right-item->bbox.left) * scale;
  native_width  = gdk_pixbuf_get_width(item->image);
  native_height = gdk_pixbuf_get_height(item->image);
  native_thr_w = MAX(2, native_width * native_threshold_pct / 100);
  native_thr_h = MAX(2, native_height * native_threshold_pct / 100);
  use_native = (fabs(native_width - required_width) < native_thr_w && 
		fabs(native_height - required_height) < native_thr_h);
  /* printf("height diff %f; threshold is %f\n",fabs(native_height - required_height), native_thr_h); */
  /* if(use_native) printf("using NATIVE\n"); */
  /* else printf("rescaling w/ scale %f\n",scale); */
  if (!use_native) 
    return gdk_pixbuf_scale_simple(item->image, required_width, 
				   required_height, GDK_INTERP_BILINEAR);    
  else
    return gdk_pixbuf_copy(item->image);
}

void import_img_as_clipped_item()
{
  int bufsz, nitems = 1, nimages = 1;
  unsigned char *buf, *p;
  GtkTargetEntry target;
  GdkPixbuf *pixbuf;
  struct Item *item;
  struct BBox selection_bbox;
  double bbox_scale = 1 / ui.image_one_to_one_zoom;
  ImgSerContext isc;
  char *paste_fname_base = "paste_";
  
  pixbuf = gtk_clipboard_wait_for_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
  /* printf(got image..."); */
  if(pixbuf==NULL){
    /* open failed */
    ui.cur_item = NULL;
    ui.cur_item_type = ITEM_NONE;
    return;
  }
  item = g_new(struct Item, 1);
  item->type = ITEM_IMAGE;
  item->image_pasted = TRUE;
  item->image_path = NULL;
  item->image_id = journal.image_id_counter++;
  set_image_path_name(item, paste_fname_base, item->image_id);
  item->canvas_item = NULL;

  item->bbox.left = 0;
  item->bbox.top = 0;
  item->image = pixbuf;

  /* How to set the bbox for a pasted image: bb := pixels /
     ui.image_one_to_one_zoom (default; rescaled if it makes bbox bigger than
     available canvas space).  The apparent size of the bbox on screen (in
     pixels) is bb * ui.zoom.  Thus, if ui.zoom = 4/3 (default) and
     ui.image_one_to_one_zoom = 4/3 also, then a 100-pixel image will have bb
     = 75, but will display at 100 pixels when ui.zoom = 4/3.
   */
  if ((ui.cur_page->width-item->bbox.left) < gdk_pixbuf_get_width(item->image) * bbox_scale)
    //set scale so that it does not extend too far to the right
    bbox_scale=(ui.cur_page->width-item->bbox.left) / (gdk_pixbuf_get_width(item->image));
  if ((ui.cur_page->height-item->bbox.top) < gdk_pixbuf_get_height(item->image) * bbox_scale) 
    //set scale so that it does not extend too far to the bottom
    bbox_scale=(ui.cur_page->height-item->bbox.top) / (gdk_pixbuf_get_height(item->image) * bbox_scale);

  item->bbox.right = item->bbox.left + gdk_pixbuf_get_width(item->image) * bbox_scale;
  item->bbox.bottom = item->bbox.top + gdk_pixbuf_get_height(item->image) * bbox_scale;

  item->image_scaled = NULL;

  g_memmove(&(item->brush), ui.cur_brush, sizeof(struct Brush));

  make_bbox_copy(&selection_bbox, &item->bbox, DEFAULT_PADDING);
  // now we put this item in a buffer in the same way that a selection item
  // would be processed on copy
  isc = serialize_image(item->image);
  bufsz = buffer_size_for_header() + buffer_size_for_item(item);
  bufsz += buffer_size_for_serialized_image(isc);
  p = buf = g_malloc(bufsz);
  put_header_in_buffer(&bufsz, &nitems, &selection_bbox, &p);
  put_item_in_buffer(item, &p);
  put_image_data_in_buffer(&isc, &p);

  g_object_unref(item->image); // image_scaled is null, so no need to unref
  g_free(item->image_path);
  g_free(item);
   
   // finish setting this up as a regular selection
  target.target = "_XOURNAL";
  target.flags = 0;
  target.info = 0;

  gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), 
			      &target, 1,
			      callback_clipboard_get, callback_clipboard_clear, buf);
}

void insert_image(GdkEvent *event, struct Item *item)
{
  double pt[2];
  GtkTextBuffer *buffer;
  GnomeCanvasItem *canvas_item;
  GdkColor color;
  GtkWidget *dialog;
  GtkFileFilter *filt_all;
  GtkFileFilter *filt_gdkimage;
  char *filename;
  GdkPixbuf *pixbuf;
  double scale=1;
  char *insert_fname_base = "insert_";
  
  dialog = gtk_file_chooser_dialog_new(_("Insert Image"), GTK_WINDOW (winMain),
     GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
     GTK_STOCK_OPEN, GTK_RESPONSE_OK, NULL);
#ifdef FILE_DIALOG_SIZE_BUGFIX
  gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);
#endif
     
  filt_all = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_all, _("All files"));
  gtk_file_filter_add_pattern(filt_all, "*");
  filt_gdkimage = gtk_file_filter_new();
  gtk_file_filter_set_name(filt_gdkimage, _("supported image files"));
  gtk_file_filter_add_pixbuf_formats(filt_gdkimage);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_gdkimage);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filt_all);

  if (ui.default_image != NULL) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER (dialog), ui.default_image);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
    gtk_widget_destroy(dialog);
    return;
  }
  filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
  gtk_widget_destroy(dialog);
  
  if (filename == NULL) return; // nothing selected
 
  if (ui.default_image != NULL) g_free(ui.default_image);
  ui.default_image = g_strdup(filename);
  
  get_pointer_coords(event, pt);

  ui.cur_item_type = ITEM_IMAGE;

  pixbuf=gdk_pixbuf_new_from_file(filename, NULL);
  if(pixbuf==NULL){
	  /* open failed */
	  dialog = gtk_message_dialog_new(GTK_WINDOW (winMain), GTK_DIALOG_DESTROY_WITH_PARENT,
	    GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Error opening image '%s'"), filename);
	  gtk_dialog_run(GTK_DIALOG(dialog));
	  gtk_widget_destroy(dialog);
	  g_free(filename);
	  ui.cur_item = NULL;
	  ui.cur_item_type = ITEM_NONE;
	  return;
  }
	  
  if (item==NULL) {
    item = g_new(struct Item, 1);
    item->type = ITEM_IMAGE;
    item->image_id = journal.image_id_counter++;
    set_image_path_name(item, insert_fname_base, item->image_id);
    item->canvas_item = NULL;
    item->bbox.left = pt[0];
    item->bbox.top = pt[1];
    item->image = pixbuf;
    item->image_pasted = FALSE;
    if(1>(ui.cur_page->width-item->bbox.left)/gdk_pixbuf_get_width(item->image)) //set scale so that it does not extend too far to the right
	scale=(ui.cur_page->width-item->bbox.left)/gdk_pixbuf_get_width(item->image);
    if(scale>(ui.cur_page->height-item->bbox.top)/gdk_pixbuf_get_height(item->image)) //set scale so that it does not extend too far to the bottom
	scale=(ui.cur_page->height-item->bbox.top)/gdk_pixbuf_get_height(item->image);
    item->image_scaled=gdk_pixbuf_scale_simple(item->image,
					scale*gdk_pixbuf_get_width(item->image),
					scale*gdk_pixbuf_get_height(item->image),
					GDK_INTERP_HYPER);
    item->bbox.right = pt[0]+gdk_pixbuf_get_width(item->image_scaled);
    item->bbox.bottom = pt[1]+gdk_pixbuf_get_height(item->image_scaled);
    g_memmove(&(item->brush), ui.cur_brush, sizeof(struct Brush));
    ui.cur_layer->items = g_list_append(ui.cur_layer->items, item);
    ui.cur_layer->nitems++;
  }
  
  item->type = ITEM_IMAGE;
  ui.cur_item = item;
  
  
  canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
                             GNOME_TYPE_CANVAS_PIXBUF,
				"anchor",GTK_ANCHOR_NW,
				"height-in-pixels",0,
				"width-in-pixels",0,
				"x-in-pixels",0,
				"y-in-pixels",0,
				"pixbuf",item->image_scaled,
				"x", item->bbox.left, 
				"y", item->bbox.top,
				"height",item->bbox.bottom-item->bbox.top, 
				"width", item->bbox.right-item->bbox.left,  
				NULL);

  if (item->canvas_item!=NULL) {
    lower_canvas_item_to(ui.cur_layer->group, canvas_item, item->canvas_item);
    gtk_object_destroy(GTK_OBJECT(item->canvas_item));
  }
  item->canvas_item = canvas_item;

  // add undo information
  prepare_new_undo();
  undo->type = ITEM_IMAGE;
  undo->item = ui.cur_item;
  undo->layer = ui.cur_layer;
  
  ui.cur_item = NULL;
  ui.cur_item_type = ITEM_NONE;
}
