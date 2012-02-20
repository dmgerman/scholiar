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

void callback_clipboard_get_pp(GtkClipboard *clipboard,
                            GtkSelectionData *selection_data,
                            guint info, gpointer user_data)
{
  int length;
  
  g_memmove(&length, user_data, sizeof(int));
  gtk_selection_data_set(selection_data,
     gdk_atom_intern("_XOURNAL_PP", FALSE), 8, user_data, length);
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

int buffer_size_for_special(int item_type) {
  int bufsz = sizeof(int); // type info
  switch (item_type) {
  case ITEM_COPY_PAGE:
    bufsz += sizeof(int) 
      + sizeof(double) 
      + sizeof(double) 
      + sizeof(double) 
      + sizeof(double) 
      + sizeof(struct Background);
    break;
  case ITEM_COPY_LAYER:
    bufsz += sizeof(int) + sizeof(gboolean);
    break;
  default:
    break;
  }
  return bufsz;
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
  return (2*sizeof(int) //bufsz, nitems
    + sizeof(struct BBox)); // bbox
}

int buffer_size_for_page_header()
{
  return 2*sizeof(int); //bufsz, nitems
}

int buffer_size_for_serialized_image(ImgSerContext isc)
{
  return (sizeof(gsize) + isc.stream_length);
}

// allocates buffers for data and serialized images
struct PageCopyContext *prepare_page_copy_buffers(struct Page *p) {
  struct PageCopyContext *pcc = g_new(struct PageCopyContext, 1);
  GList *llist, *ilist;
  int n_it_tmp, n_im_tmp;
  ImgSerContext **serialized_images;
  int i = 0;
  pcc->pg = p;
  pcc->nitems = 0;
  pcc->serialized_images = g_malloc(p->nlayers * sizeof(*(pcc->serialized_images)));
  pcc->nimages = g_malloc(p->nlayers * sizeof(int));
  pcc->bufsz = buffer_size_for_page_header();  
  pcc->bufsz += buffer_size_for_special(ITEM_COPY_PAGE);
  pcc->nitems++; // page itself
  i = 0;
  for (llist = p->layers; llist != NULL; llist = llist->next) {
    pcc->nitems++; // layer is a buffer item
    pcc->bufsz += buffer_size_for_special(ITEM_COPY_LAYER);
    ilist = ((struct Layer*)llist->data)->items; // this layer's items
    get_nitems_update_bufsize(ilist, &pcc->bufsz, &n_it_tmp, &n_im_tmp);
    pcc->serialized_images[i] = g_new(struct ImgSerContext, n_im_tmp);
    pcc->nimages[i] = n_im_tmp;
    pcc->nitems += n_it_tmp;
    if (n_im_tmp > 0)
      update_bufsize_and_ser_images(ilist, &pcc->bufsz, &(pcc->serialized_images[i]));
    i++;
  }
  pcc->buf = g_malloc(pcc->bufsz);
  return pcc; 
}

void free_image_ser_buffers_and_pcc(struct PageCopyContext *pcc) {
  int i, j;
  for (i = 0; i < pcc->pg->nlayers; i++) {
    for (j = 0; j < pcc->nimages[i]; j++)
      g_free(pcc->serialized_images[i][j].image_data);
    g_free(pcc->serialized_images[i]);
  }
  g_free(pcc->serialized_images);
  g_free(pcc->nimages);
  g_free(pcc);
}

void put_page_in_buffer(struct PageCopyContext *pcc) {
  guchar **pp = &(pcc->buf);
  ImgSerContext **ser_images = pcc->serialized_images;
  GList *llist, *ilist;
  int i = 0;
  //store header:
  copy_to_buffer_advance_ptr(pp, &pcc->bufsz, sizeof(int));
  copy_to_buffer_advance_ptr(pp, &pcc->nitems, sizeof(int)); 
  //store page:
  put_page_metadata_in_buffer(pcc->pg, pp);
  for (llist = pcc->pg->layers; llist != NULL; llist = llist->next) {
    put_layer_metadata_in_buffer((struct Layer*)llist->data, pp);
    ilist = ((struct Layer*)llist->data)->items; // this layer's items
    populate_buffer(ilist, ser_images[i], pp);
    i++;
  }    
}

void put_page_metadata_in_buffer(struct Page *p, guchar **pp) {
  int item_type = ITEM_COPY_PAGE;
  guchar *start;
  start = *pp;
  copy_to_buffer_advance_ptr(pp, &item_type, sizeof(int));
  copy_to_buffer_advance_ptr(pp, &p->nlayers, sizeof(int));
  copy_to_buffer_advance_ptr(pp, &p->height, sizeof(double));
  copy_to_buffer_advance_ptr(pp, &p->width, sizeof(double));
  copy_to_buffer_advance_ptr(pp, &p->hoffset, sizeof(double));
  copy_to_buffer_advance_ptr(pp, &p->voffset, sizeof(double));
  copy_to_buffer_advance_ptr(pp, p->bg, sizeof(struct Background));
}


void put_layer_metadata_in_buffer(struct Layer *l, guchar **pp) {
  int item_type = ITEM_COPY_LAYER;
  guchar *start = *pp; // dbg
  copy_to_buffer_advance_ptr(pp, &item_type, sizeof(int));
  copy_to_buffer_advance_ptr(pp, &l->nitems, sizeof(int));
  copy_to_buffer_advance_ptr(pp, &l->visible, sizeof(gboolean));
}

void put_item_in_buffer(struct Item *item, guchar **pp)
{
  int val;
  guchar *start = *pp; // dbg
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

void get_nitems_update_bufsize(GList* items_list, int* bufsz, int* nitems, int* nimages)
{
  GList *list;
  struct Item *item;
  *nitems = 0; *nimages = 0;
  for (list = items_list; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    (*nitems)++;
    if (item->type == ITEM_IMAGE) (*nimages)++;
    (*bufsz) += buffer_size_for_item(item);
  }
}

void update_bufsize_and_ser_images(GList* items_list, int* bufsz, ImgSerContext** serialized_images)
{
  GList *list;
  struct Item *item;
  int i = 0;
  for (list = items_list; list != NULL; list = list->next) {
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

void populate_buffer(GList* items_list, struct ImgSerContext *serialized_images, guchar **pp)
{
  GList *list;
  struct Item *item;
  int i = 0;
  for (list = items_list; list != NULL; list = list->next) {
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
  get_nitems_update_bufsize(ui.selection->items, &bufsz, &nitems, &nimages);
  serialized_images = g_new(struct ImgSerContext, nimages);
  if (nimages > 0)
    update_bufsize_and_ser_images(ui.selection->items, &bufsz, &serialized_images);

  p = buf = g_malloc(bufsz);
  put_header_in_buffer(&bufsz, &nitems, &ui.selection->bbox, &p);
  populate_buffer(ui.selection->items, serialized_images, &p);

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

// NOTE: this function assumes that ITEM_TYPE field has already
// been consumed in the buffer and that the memory for data on heap
// has ALREADY been allocated!
void get_page_from_buffer(struct Page *page, guchar **pp)
{
  copy_from_buffer_advance_ptr(&page->nlayers, pp, sizeof(int));
  copy_from_buffer_advance_ptr(&page->height, pp, sizeof(double));
  copy_from_buffer_advance_ptr(&page->width, pp, sizeof(double));
  copy_from_buffer_advance_ptr(&page->hoffset, pp, sizeof(double));
  copy_from_buffer_advance_ptr(&page->voffset, pp, sizeof(double));
  copy_from_buffer_advance_ptr(page->bg, pp, sizeof(struct Background));
  page->bg->canvas_item = NULL;
  page->bg->pixbuf = NULL;
  page->bg->filename = NULL;
  page->bg->type = BG_SOLID;
  page->group = (GnomeCanvasGroup *) 
    gnome_canvas_item_new(gnome_canvas_root(canvas), gnome_canvas_clipgroup_get_type(), NULL);
  page->layers = NULL;
}
// NOTE: this function assumes that ITEM_TYPE field has already
// been consumed in the buffer!
void get_layer_from_buffer(struct Layer *l, guchar **pp)
{
  copy_from_buffer_advance_ptr(&l->nitems, pp, sizeof(int));
  copy_from_buffer_advance_ptr(&l->visible, pp, sizeof(gboolean));
}

// NOTE: this function assumes that *item already contains the read-in item type
// Sample usage:
/* copy_from_buffer_advance_ptr(&item->type, &p, sizeof(int)); */
/* get_item_from_buffer(item, &p, 0., 0.); */
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
  
  ui.selection->bbox = bbox_add_offset_lrtb(ui.selection->bbox, hoffset, hoffset, voffset, voffset);

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

/* 
 * Create a copy by adapting clipboard selection buffer for a single item
 */
struct Item* create_item_copy(struct Item *orig) {
  int bufsz = 0, nitems = 1, nimages = 0;
  unsigned char *buf, *p;
  ImgSerContext serialized_image;
  struct Item *item = g_new(struct Item, 1);
  
  bufsz += buffer_size_for_item(orig);
  if (orig->type == ITEM_IMAGE) {
    serialized_image = serialize_image(orig->image);
    bufsz += buffer_size_for_serialized_image(serialized_image);
  }
  buf = p = g_malloc(bufsz);
  put_item_in_buffer(orig, &p);
  if (orig->type == ITEM_IMAGE) 
    put_image_data_in_buffer(&serialized_image, &p);
  p = buf; // reset p to point back to buf start
  
  copy_from_buffer_advance_ptr(&item->type, &p, sizeof(int));
  get_item_from_buffer(item, &p, 0., 0.);

  g_free(buf);
  return item;
}

struct Layer* create_layer_copy(struct Layer *orig, struct Page *enclosing_page) {
  struct Layer *l = g_new(struct Layer, 1);
  struct Item *new_item;
  GList *list;
  init_layer(l);
  l->nitems = orig->nitems;
  l->items = g_list_copy(orig->items); // shallow copy
  for (list = l->items; list != NULL; list = list->next) {
    new_item = create_item_copy((struct Item *)list->data);
    list->data = (gpointer)new_item;
  }
  l->group = (GnomeCanvasGroup *) 
    gnome_canvas_item_new(enclosing_page->group, gnome_canvas_group_get_type(), NULL);
  return l;
}

void copy_page() {
  guchar *buf;
  GtkTargetEntry target;
  struct PageCopyContext *pcc = prepare_page_copy_buffers(ui.cur_page);
  buf = pcc->buf;

  put_page_in_buffer(pcc);

  target.target = "_XOURNAL_PP";
  target.flags = 0;
  target.info = 0;
  
  gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), 
       &target, 1, callback_clipboard_get_pp, callback_clipboard_clear, buf);
  free_image_ser_buffers_and_pcc(pcc);
}

struct Page* paste_page() {
  struct Page *pg = (struct Page *) g_memdup(&ui.default_page, sizeof(struct Page));
  struct Layer *l;
  struct Item *item;
  GtkSelectionData *sel_data;
  unsigned char *p;
  int nitems, itemtype;
  pg->bg = g_new(struct Background, 1);

  ui.cur_item_type = ITEM_PASTE;
  sel_data = gtk_clipboard_wait_for_contents(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
					     gdk_atom_intern("_XOURNAL_PP", FALSE));
  ui.cur_item_type = ITEM_NONE;
  if (sel_data == NULL) return NULL; // paste failed
  p = sel_data->data + sizeof(int);
  copy_from_buffer_advance_ptr(&nitems, &p, sizeof(int));
  while (nitems-- > 0) {
    copy_from_buffer_advance_ptr(&itemtype, &p, sizeof(int));
    if (itemtype == ITEM_COPY_PAGE) {
      get_page_from_buffer(pg, &p);
      make_page_clipbox(pg);
      update_canvas_bg(pg);
    } else if (itemtype == ITEM_COPY_LAYER) {
      l = g_new(struct Layer, 1);
      get_layer_from_buffer(l, &p);
      l->items = NULL;
      l->group = (GnomeCanvasGroup *) 
	gnome_canvas_item_new(pg->group, gnome_canvas_group_get_type(), NULL);
      pg->layers = g_list_append(pg->layers, l);
    } else {
      item = g_new(struct Item, 1);
      item->type = itemtype;
      l->items = g_list_append(l->items, item);
      get_item_from_buffer(item, &p, 0., 0.); // zero h/v offsets
      make_canvas_item_one(l->group, item);
    }
  }
  journal.pages = g_list_insert(journal.pages, pg, ui.pageno + 1);
  journal.npages++;
  do_switch_page(ui.pageno + 1, TRUE, TRUE);
  gtk_selection_data_free(sel_data);
  
  prepare_new_undo();
  undo->type = ITEM_PASTE_PAGE;
  undo->val = ui.pageno;
  undo->page = pg;
  return pg;
} 

struct Page* duplicate_page() {
  GList *llist, *itemlist;
  struct Layer *l;
  struct Page *pg = (struct Page *) g_memdup(ui.cur_page, sizeof(struct Page));
  pg->layers = NULL;
  pg->nlayers = 0;
  copy_page_background(pg, ui.cur_page);
  pg->group = (GnomeCanvasGroup *) 
    gnome_canvas_item_new(gnome_canvas_root(canvas), gnome_canvas_clipgroup_get_type(), NULL);
  make_page_clipbox(pg);
  update_canvas_bg(pg);

  for (llist = ui.cur_page->layers; llist != NULL; llist = llist->next) {
    //create a duplicate of the layer
    l = create_layer_copy((struct Layer *)llist->data, pg);
    //append via  g_list_append(pg->layers, 
    pg->layers = g_list_append(pg->layers, l);
    //increment the layer number counter
    pg->nlayers++;
    for (itemlist = l->items; itemlist != NULL; itemlist = itemlist->next)
      make_canvas_item_one(l->group, (struct Item *)itemlist->data);
  }

  //insert the page into the journal pages list
  journal.pages = g_list_insert(journal.pages, pg, ui.pageno + 1);
  journal.npages++;
  do_switch_page(ui.pageno + 1, TRUE, TRUE);
  
  prepare_new_undo();
  undo->type = ITEM_PASTE_PAGE;
  undo->val = ui.pageno;
  undo->page = pg;
  
  return pg;
}
