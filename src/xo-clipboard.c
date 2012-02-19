#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgnomecanvas/libgnomecanvas.h>
#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-misc.h"
#include "xo-paint.h"
#include "xo-image.h"
#include "xo-clipboard.h"
#include "xo-selection.h"

void callback_clipboard_get(GtkClipboard *clipboard,
                            GtkSelectionData *selection_data,
                            guint info, gpointer user_data)
{
  int length;
  
  g_memmove(&length, user_data, sizeof(int));
  gtk_selection_data_set(selection_data,
     gdk_atom_intern("_XOURNAL", FALSE), 8, user_data, length);
}

void callback_clipboard_clear(GtkClipboard *clipboard, gpointer user_data)
{
  g_free(user_data);
}

void copy_to_buffer_advance_ptr(guchar **to, gpointer from, gsize size)
{
  g_memmove(*to, from, size); *to+=size;
}

void copy_from_buffer_advance_ptr(gpointer to, guchar **from, gsize size)
{
  g_memmove(to, *from, size); *from+=size;
}

// returns the number of bytes needed to store a particular item, including the type_id field
// Note that this has to correlate exactly with _what_ is being put in the buffer!
int buffer_size_for_item(struct Item *item)
{
  int bufsz = 0;
  bufsz += sizeof(int); // will always contain type info
  switch (item->type) {
  case ITEM_STROKE:
    bufsz+= sizeof(struct Brush) // brush
      + sizeof(int) // num_points
      + 2*item->path->num_points*sizeof(double); // the points
    if (item->brush.variable_width)
      bufsz += (item->path->num_points-1)*sizeof(double); // the widths
    break;
  case ITEM_TEXT:
    bufsz+= sizeof(struct Brush) // brush
      + 2*sizeof(double) // bbox upper-left
      + sizeof(int) // text len
      + strlen(item->text)+1 // text
      + sizeof(int) // font_name len
      + strlen(item->font_name)+1 // font_name
      + sizeof(double); // font_size
    break;
  case ITEM_IMAGE:
    bufsz += sizeof(int) //path strlen
      + strlen(item->image_path)+1
      + sizeof(gboolean) //image_pasted
      + 4*sizeof(double) // bbox 
      + sizeof(unsigned int); //image_id
    break;
  default: // will just return sizeof(int) for type field storage
    break;
  }
  return bufsz;
}

// buffer layout:
// bufsz, nitems, bbox, [item]*
// everything before the item entries is considered a header
int buffer_size_for_header()
{
  return (2*sizeof(int) // bufsz, nitems
    + sizeof(struct BBox)); // bbox
}

int buffer_size_for_serialized_image(ImgSerContext isc)
{
  return (sizeof(gsize) + isc.stream_length);
}

void put_item_in_buffer(struct Item *item, guchar **pp)
{
  int val;
  copy_to_buffer_advance_ptr(pp, &item->type, sizeof(int));
  switch (item->type) {
  case ITEM_STROKE:
    copy_to_buffer_advance_ptr(pp, &item->brush, sizeof(struct Brush));
    copy_to_buffer_advance_ptr(pp, &item->path->num_points, sizeof(int));
    copy_to_buffer_advance_ptr(pp, item->path->coords, 2*item->path->num_points*sizeof(double));
    if (item->brush.variable_width)
      copy_to_buffer_advance_ptr(pp, item->widths, (item->path->num_points-1)*sizeof(double));
    break;
  case ITEM_TEXT:
    copy_to_buffer_advance_ptr(pp, &item->brush, sizeof(struct Brush));
    copy_to_buffer_advance_ptr(pp, &item->bbox.left, sizeof(double));
    copy_to_buffer_advance_ptr(pp, &item->bbox.top, sizeof(double));
    val = strlen(item->text);
    copy_to_buffer_advance_ptr(pp, &val, sizeof(int));
    copy_to_buffer_advance_ptr(pp, item->text, val+1);
    val = strlen(item->font_name);
    copy_to_buffer_advance_ptr(pp, &val, sizeof(int));
    copy_to_buffer_advance_ptr(pp, item->font_name, val+1);
    copy_to_buffer_advance_ptr(pp, &item->font_size, sizeof(double));
    break;
  case ITEM_IMAGE:
    val = strlen(item->image_path);
    copy_to_buffer_advance_ptr(pp, &val, sizeof(int));
    copy_to_buffer_advance_ptr(pp, item->image_path, val+1);
    copy_to_buffer_advance_ptr(pp, &item->image_pasted, sizeof(gboolean));
    copy_to_buffer_advance_ptr(pp, &item->bbox.left, sizeof(double));
    copy_to_buffer_advance_ptr(pp, &item->bbox.top, sizeof(double));
    copy_to_buffer_advance_ptr(pp, &item->bbox.right, sizeof(double));
    copy_to_buffer_advance_ptr(pp, &item->bbox.bottom, sizeof(double));
    copy_to_buffer_advance_ptr(pp, &item->image_id, sizeof(unsigned int));
    break;
  default:
    break;
  }
}

void get_nitems_update_bufsize(int* bufsz, int* nitems, int* nimages)
{
  GList *list;
  struct Item *item;
  *nitems = 0; *nimages = 0;
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    (*nitems)++;
    if (item->type == ITEM_IMAGE) (*nimages)++;
    (*bufsz) += buffer_size_for_item(item);
  }
}

void update_bufsize_and_ser_images(int* bufsz, ImgSerContext** serialized_images)
{
  GList *list;
  struct Item *item;
  int i = 0;
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    if (item->type == ITEM_IMAGE) {
      (*serialized_images)[i] = serialize_image(item->image);
      (*bufsz) += buffer_size_for_serialized_image((*serialized_images)[i]);
      i++;
    }
  }
}

void put_header_in_buffer(int *bufsz, int *nitems, struct BBox *bbox, guchar **pp) {
  copy_to_buffer_advance_ptr(pp, bufsz, sizeof(int));
  copy_to_buffer_advance_ptr(pp, nitems, sizeof(int)); 
  copy_to_buffer_advance_ptr(pp, bbox, sizeof(struct BBox));
}

void put_image_data_in_buffer(struct ImgSerContext *isc, guchar **pp)
{
  copy_to_buffer_advance_ptr(pp, &isc->stream_length, sizeof(gsize));
  copy_to_buffer_advance_ptr(pp, isc->image_data, isc->stream_length);
}

void populate_buffer(struct ImgSerContext *serialized_images, guchar **pp)
{
  GList *list;
  struct Item *item;
  int i = 0;
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    put_item_in_buffer(item, pp);
    if (item->type == ITEM_IMAGE) 
      put_image_data_in_buffer(&serialized_images[i++], pp);
  }
}

void selection_to_clip(void) 
{
  int bufsz = 0, nitems = 0, nimages = 0, val, i, len;
  unsigned char *buf, *p;
  GList *list;
  struct Item *item;
  GtkTargetEntry target;
  ImgSerContext *serialized_images;
  
  if (ui.selection == NULL) return;
  bufsz = buffer_size_for_header();  
  get_nitems_update_bufsize(&bufsz, &nitems, &nimages);
  serialized_images = g_new(struct ImgSerContext, nimages);
  if (nimages > 0)
    update_bufsize_and_ser_images(&bufsz, &serialized_images);

  p = buf = g_malloc(bufsz);
  put_header_in_buffer(&bufsz, &nitems, &ui.selection->bbox, &p);
  populate_buffer(serialized_images, &p);

  target.target = "_XOURNAL";
  target.flags = 0;
  target.info = 0;
  
  gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), 
       &target, 1, callback_clipboard_get, callback_clipboard_clear, buf);

  for (i=0; i < nimages; i++)
      g_free(serialized_images[i].image_data);
  g_free(serialized_images);
}

void clipboard_paste(void)
{
  clipboard_paste_with_offset(FALSE, 0, 0);
} 

void clipboard_paste_get_offset(double *hoffset, double *voffset)
{
  double cx, cy;
  int sx, sy, wx, wy;
 // find by how much we translate the pasted selection
  gnome_canvas_get_scroll_offsets(canvas, &sx, &sy);
  gdk_window_get_geometry(GTK_WIDGET(canvas)->window, NULL, NULL, &wx, &wy, NULL);
  gnome_canvas_window_to_world(canvas, sx + wx/2, sy + wy/2, &cx, &cy);
  cx -= ui.cur_page->hoffset;
  cy -= ui.cur_page->voffset;
  if (cx + (ui.selection->bbox.right-ui.selection->bbox.left)/2 > ui.cur_page->width)
    cx = ui.cur_page->width - (ui.selection->bbox.right-ui.selection->bbox.left)/2;
  if (cx - (ui.selection->bbox.right-ui.selection->bbox.left)/2 < 0)
    cx = (ui.selection->bbox.right-ui.selection->bbox.left)/2;
  if (cy + (ui.selection->bbox.bottom-ui.selection->bbox.top)/2 > ui.cur_page->height)
    cy = ui.cur_page->height - (ui.selection->bbox.bottom-ui.selection->bbox.top)/2;
  if (cy - (ui.selection->bbox.bottom-ui.selection->bbox.top)/2 < 0)
    cy = (ui.selection->bbox.bottom-ui.selection->bbox.top)/2;
  *hoffset = cx - (ui.selection->bbox.right+ui.selection->bbox.left)/2;
  *voffset = cy - (ui.selection->bbox.top+ui.selection->bbox.bottom)/2;
}

void get_item_from_buffer(struct Item *item, guchar **pp, double hoffset, double voffset)
{
  int i, npts, len;
  struct ImgSerContext isc;
  double *pf;
  GdkPixbuf *tmp_pixbuf_ptr;
  switch (item->type) {
  case ITEM_STROKE:
    copy_from_buffer_advance_ptr(&item->brush, pp, sizeof(struct Brush));
    copy_from_buffer_advance_ptr(&npts, pp, sizeof(int));
    item->path = gnome_canvas_points_new(npts);
    pf = (double *)*pp;
    for (i=0; i<npts; i++) {
      item->path->coords[2*i] = pf[2*i] + hoffset;
      item->path->coords[2*i+1] = pf[2*i+1] + voffset;
    }
    *pp+= 2*item->path->num_points*sizeof(double);
    if (item->brush.variable_width) {
      item->widths = g_memdup(*pp, (item->path->num_points-1)*sizeof(double));
      *pp+= (item->path->num_points-1)*sizeof(double);
    } 
    else item->widths = NULL;
    update_item_bbox(item);
    break;
 case ITEM_TEXT:
   copy_from_buffer_advance_ptr(&item->brush, pp, sizeof(struct Brush));
   copy_from_buffer_advance_ptr(&item->bbox.left, pp, sizeof(double));
   copy_from_buffer_advance_ptr(&item->bbox.top, pp, sizeof(double));
   item->bbox.left += hoffset;
   item->bbox.top += voffset;
   copy_from_buffer_advance_ptr(&len, pp, sizeof(int));
   item->text = g_malloc(len+1);
   copy_from_buffer_advance_ptr(item->text, pp, len+1);
   copy_from_buffer_advance_ptr(&len, pp, sizeof(int));
   item->font_name = g_malloc(len+1);
   copy_from_buffer_advance_ptr(item->font_name, pp, len+1);
   copy_from_buffer_advance_ptr(&item->font_size, pp, sizeof(double));
   break;
  case ITEM_IMAGE:
    copy_from_buffer_advance_ptr(&len, pp, sizeof(int));
    item->image_path = g_malloc(len+1);
    copy_from_buffer_advance_ptr(item->image_path, pp, len+1);
    copy_from_buffer_advance_ptr(&item->image_pasted, pp, sizeof(gboolean));

    copy_from_buffer_advance_ptr(&item->bbox.left, pp, sizeof(double));
    copy_from_buffer_advance_ptr(&item->bbox.top, pp, sizeof(double));
    copy_from_buffer_advance_ptr(&item->bbox.right, pp, sizeof(double));
    copy_from_buffer_advance_ptr(&item->bbox.bottom, pp, sizeof(double));
    item->bbox.left += hoffset;
    item->bbox.right += hoffset;
    item->bbox.top += voffset;
    item->bbox.bottom += voffset;

    copy_from_buffer_advance_ptr(&item->image_id, pp, sizeof(unsigned int));
    copy_from_buffer_advance_ptr(&isc.stream_length, pp, sizeof(gsize));
    isc.image_data = g_malloc(isc.stream_length);
    copy_from_buffer_advance_ptr(isc.image_data, pp, isc.stream_length);

    item->image = deserialize_image(isc);
    item->image_scaled = NULL;
    g_free(isc.image_data);
    break;
  default:
    break;
  }
}

// if use_provided_offset == FALSE, hoffset and voffset parameters are
// ignored and are recomputed inside the function (effectively it's the same
// as the old clipboard_paste)
void clipboard_paste_with_offset(gboolean use_provided_offset, double hoffset, double voffset) 
{
  GnomeCanvasItem *canvas_item;
  GtkSelectionData *sel_data;
  unsigned char *p;
  int nitems, npts, i, len, im_refs, im_sc_refs;
  struct Item *item;
  double *pf;
  gboolean clipboard_has_image;

  if (ui.cur_layer == NULL) return;
  
  clipboard_has_image = gtk_clipboard_wait_is_image_available(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
  if (clipboard_has_image) 
    import_img_as_clipped_item();

  ui.cur_item_type = ITEM_PASTE;
  sel_data = gtk_clipboard_wait_for_contents(
      gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
      gdk_atom_intern("_XOURNAL", FALSE));
  ui.cur_item_type = ITEM_NONE;
  if (sel_data == NULL) return; // paste failed
  
  p = sel_data->data + sizeof(int);
  copy_from_buffer_advance_ptr(&nitems, &p, sizeof(int));
  reset_selection();
  get_new_selection(ITEM_SELECTRECT, ui.cur_layer);
  copy_from_buffer_advance_ptr(&ui.selection->bbox, &p, sizeof(struct BBox));

  if (! use_provided_offset)
    clipboard_paste_get_offset(&hoffset, &voffset);
  
  ui.selection->bbox.left += hoffset;
  ui.selection->bbox.right += hoffset;
  ui.selection->bbox.top += voffset;
  ui.selection->bbox.bottom += voffset;

  ui.selection->canvas_item = canvas_item_new_for_selection(ITEM_SELECTRECT);
  make_dashed(ui.selection->canvas_item);

  while (nitems-- > 0) {
    item = g_new(struct Item, 1);
    ui.selection->items = g_list_append(ui.selection->items, item);
    ui.cur_layer->items = g_list_append(ui.cur_layer->items, item);
    ui.cur_layer->nitems++;
    copy_from_buffer_advance_ptr(&item->type, &p, sizeof(int));
    get_item_from_buffer(item, &p, hoffset, voffset); // offsets needed for setting item bbox
    make_canvas_item_one(ui.cur_layer->group, item);
  }

  prepare_new_undo();
  undo->type = ITEM_PASTE;
  undo->layer = ui.cur_layer;
  undo->itemlist = g_list_copy(ui.selection->items);  
  
  gtk_selection_data_free(sel_data);
  update_copy_paste_enabled();
  update_color_menu();
  update_thickness_buttons();
  update_color_buttons();
  update_font_button();  
  update_cursor(); // FIXME: can't know if pointer is within selection!
}

struct Layer* create_layer_copy(struct Layer* orig) {
  struct Layer *l = g_new(struct Layer, 1);
  init_layer(l);
  
}

struct Page* duplicate_page() {
  GList *layer, *item;
  struct Page *pg = new_page(ui.cur_page);
  for (layer = ui.cur_page->layers; layer != NULL; layer = layer->next) {
    //create a duplicate of the layer
    //append via  g_list_append(pg->layers, 
    //increment the layer number counter
  }
  //set the ui.cur_page pointer to point the new page
  //set the ui.cur_layer pointer to the appropriate layer on the new page based on the ui.layerno
    if (ui.layerno<0) ui.cur_layer = NULL;
    else ui.cur_layer = (struct Layer *)g_list_nth_data(ui.cur_page->layers, ui.layerno);
    
    //insert the page into the journal pages list
    journal.pages = g_list_insert(journal.pages, undo->page, undo->val);
    journal.npages++;
    make_canvas_items(); // re-create the canvas items
    do_switch_page(undo->val, TRUE, TRUE);
 
}
