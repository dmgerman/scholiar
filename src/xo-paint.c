#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgnomecanvas/libgnomecanvas.h>

#include <libart_lgpl/art_vpath_bpath.h>
#include <libart_lgpl/art_svp_vpath.h>
#include <libart_lgpl/art_rect_svp.h>

#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-misc.h"
#include "xo-paint.h"

/************** drawing nice cursors *********/

static char cursor_pen_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static char cursor_eraser_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x0f, 0x08, 0x08, 0x08, 0x08,
   0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0xf8, 0x0f,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static char cursor_eraser_mask[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f,
   0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void set_cursor_busy(gboolean busy)
{
  GdkCursor *cursor;
  
  if (busy) {
    cursor = gdk_cursor_new(GDK_WATCH);
    gdk_window_set_cursor(GTK_WIDGET(winMain)->window, cursor);
    gdk_window_set_cursor(GTK_WIDGET(canvas)->window, cursor);
    gdk_cursor_unref(cursor);
  }
  else {
    gdk_window_set_cursor(GTK_WIDGET(winMain)->window, NULL);
    update_cursor();
  }
  gdk_display_sync(gdk_display_get_default());
}

void update_cursor(void)
{
  GdkPixmap *source, *mask;
  GdkColor fg = {0, 0, 0, 0}, bg = {0, 65535, 65535, 65535};

  ui.is_sel_cursor = FALSE;
  if (GTK_WIDGET(canvas)->window == NULL) return;
  
  if (ui.cursor!=NULL) { 
    gdk_cursor_unref(ui.cursor);
    ui.cursor = NULL;
  }
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    ui.cursor = gdk_cursor_new(GDK_SB_V_DOUBLE_ARROW);
  else if (ui.cur_item_type == ITEM_MOVESEL)
    ui.cursor = gdk_cursor_new(GDK_FLEUR);
  else if (ui.toolno[ui.cur_mapping] == TOOL_PEN) {
    fg.red = (ui.cur_brush->color_rgba >> 16) & 0xff00;
    fg.green = (ui.cur_brush->color_rgba >> 8) & 0xff00;
    fg.blue = (ui.cur_brush->color_rgba >> 0) & 0xff00;
    source = gdk_bitmap_create_from_data(NULL, cursor_pen_bits, 16, 16);
    ui.cursor = gdk_cursor_new_from_pixmap(source, source, &fg, &bg, 7, 7);
    gdk_bitmap_unref(source);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_ERASER) {
    source = gdk_bitmap_create_from_data(NULL, cursor_eraser_bits, 16, 16);
    mask = gdk_bitmap_create_from_data(NULL, cursor_eraser_mask, 16, 16);
    ui.cursor = gdk_cursor_new_from_pixmap(source, mask, &fg, &bg, 7, 7);
    gdk_bitmap_unref(source);
    gdk_bitmap_unref(mask);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_HIGHLIGHTER) {
    source = gdk_bitmap_create_from_data(NULL, cursor_eraser_bits, 16, 16);
    mask = gdk_bitmap_create_from_data(NULL, cursor_eraser_mask, 16, 16);
    bg.red = (ui.cur_brush->color_rgba >> 16) & 0xff00;
    bg.green = (ui.cur_brush->color_rgba >> 8) & 0xff00;
    bg.blue = (ui.cur_brush->color_rgba >> 0) & 0xff00;
    ui.cursor = gdk_cursor_new_from_pixmap(source, mask, &fg, &bg, 7, 7);
    gdk_bitmap_unref(source);
    gdk_bitmap_unref(mask);
  }
  else if (ui.cur_item_type == ITEM_SELECTRECT) {
    ui.cursor = gdk_cursor_new(GDK_TCROSS);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_HAND) {
    ui.cursor = gdk_cursor_new(GDK_HAND1);
  }
  else if (ui.toolno[ui.cur_mapping] == TOOL_TEXT) {
    ui.cursor = gdk_cursor_new(GDK_XTERM);
  }
  
  gdk_window_set_cursor(GTK_WIDGET(canvas)->window, ui.cursor);
}

/* adjust the cursor shape if it hovers near a selection box */

void update_cursor_for_resize(double *pt)
{
  gboolean in_range_x, in_range_y;
  gboolean can_resize_left, can_resize_right, can_resize_bottom, can_resize_top;
  gdouble resize_margin;
  GdkCursorType newcursor;

  // if we're not even close to the box in some direction, return immediately
  resize_margin = RESIZE_MARGIN / ui.zoom;
  if (pt[0]<ui.selection->bbox.left-resize_margin || pt[0]>ui.selection->bbox.right+resize_margin
   || pt[1]<ui.selection->bbox.top-resize_margin || pt[1]>ui.selection->bbox.bottom+resize_margin)
  {
    if (ui.is_sel_cursor) update_cursor();
    return;
  }

  ui.is_sel_cursor = TRUE;

  get_possible_resize_direction(pt, &can_resize_left, &can_resize_right, &can_resize_top, &can_resize_bottom);
  if (can_resize_left) {
    if (can_resize_top) newcursor = GDK_TOP_LEFT_CORNER;
    else if (can_resize_bottom) newcursor = GDK_BOTTOM_LEFT_CORNER;
    else newcursor = GDK_LEFT_SIDE;
  }
  else if (can_resize_right) {
    if (can_resize_top) newcursor = GDK_TOP_RIGHT_CORNER;
    else if (can_resize_bottom) newcursor = GDK_BOTTOM_RIGHT_CORNER;
    else newcursor = GDK_RIGHT_SIDE;
  }
  else if (can_resize_top) newcursor = GDK_TOP_SIDE;
  else if (can_resize_bottom) newcursor = GDK_BOTTOM_SIDE;
  else newcursor = GDK_FLEUR;

  if (ui.cursor!=NULL && ui.cursor->type == newcursor) return;
  if (ui.cursor!=NULL) gdk_cursor_unref(ui.cursor);
  ui.cursor = gdk_cursor_new(newcursor);
  gdk_window_set_cursor(GTK_WIDGET(canvas)->window, ui.cursor);
}

/************** painting strokes *************/

#define SUBDIVIDE_MAXDIST 5.0

void subdivide_cur_path()
{
  int n, pieces, k;
  double *p;
  double x0, y0, x1, y1;

  for (n=0, p=ui.cur_path.coords; n<ui.cur_path.num_points-1; n++, p+=2) {
    pieces = (int)floor(hypot(p[2]-p[0], p[3]-p[1])/SUBDIVIDE_MAXDIST);
    if (pieces>1) {
      x0 = p[0]; y0 = p[1];
      x1 = p[2]; y1 = p[3];
      realloc_cur_path(ui.cur_path.num_points+pieces-1);
      g_memmove(ui.cur_path.coords+2*(n+pieces), ui.cur_path.coords+2*(n+1),
                    2*(ui.cur_path.num_points-n-1)*sizeof(double));
      p = ui.cur_path.coords+2*n;
      ui.cur_path.num_points += pieces-1;
      n += (pieces-1);
      for (k=1; k<pieces; k++) {
        p+=2;
        p[0] = x0 + k*(x1-x0)/pieces;
        p[1] = y0 + k*(y1-y0)/pieces;
      } 
    }
  }
}

void create_new_stroke(GdkEvent *event)
{
  ui.cur_item_type = ITEM_STROKE;
  ui.cur_item = g_new(struct Item, 1);
  ui.cur_item->type = ITEM_STROKE;
  g_memmove(&(ui.cur_item->brush), ui.cur_brush, sizeof(struct Brush));
  ui.cur_item->path = &ui.cur_path;
  realloc_cur_path(2);
  ui.cur_path.num_points = 1;
  get_pointer_coords(event, ui.cur_path.coords);
  
  if (ui.cur_brush->ruler) {
    ui.cur_item->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_line_get_type(),
      "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
      "fill-color-rgba", ui.cur_item->brush.color_rgba,
      "width-units", ui.cur_item->brush.thickness, NULL);
    ui.cur_item->brush.variable_width = FALSE;
  } else
    ui.cur_item->canvas_item = gnome_canvas_item_new(
      ui.cur_layer->group, gnome_canvas_group_get_type(), NULL);
}

void continue_stroke(GdkEvent *event)
{
  GnomeCanvasPoints seg;
  double *pt, current_width;

  if (ui.cur_brush->ruler) {
    pt = ui.cur_path.coords;
  } else {
    realloc_cur_path(ui.cur_path.num_points+1);
    pt = ui.cur_path.coords + 2*(ui.cur_path.num_points-1);
  } 
  
  get_pointer_coords(event, pt+2);
  
  if (ui.cur_item->brush.variable_width) {
    realloc_cur_widths(ui.cur_path.num_points);
    current_width = ui.cur_item->brush.thickness*get_pressure_multiplier(event);
    ui.cur_widths[ui.cur_path.num_points-1] = current_width;
  }
  else current_width = ui.cur_item->brush.thickness;
  
  if (ui.cur_brush->ruler)
    ui.cur_path.num_points = 2;
  else {
    if (hypot(pt[0]-pt[2], pt[1]-pt[3]) < PIXEL_MOTION_THRESHOLD/ui.zoom)
      return;  // not a meaningful motion
    ui.cur_path.num_points++;
  }

  seg.coords = pt; 
  seg.num_points = 2;
  seg.ref_count = 1;
  
  /* note: we're using a piece of the cur_path array. This is ok because
     upon creation the line just copies the contents of the GnomeCanvasPoints
     into an internal structure */

  if (ui.cur_brush->ruler)
    gnome_canvas_item_set(ui.cur_item->canvas_item, "points", &seg, NULL);
  else
    gnome_canvas_item_new((GnomeCanvasGroup *)ui.cur_item->canvas_item,
       gnome_canvas_line_get_type(), "points", &seg,
       "cap-style", GDK_CAP_ROUND, "join-style", GDK_JOIN_ROUND,
       "fill-color-rgba", ui.cur_item->brush.color_rgba,
       "width-units", current_width, NULL);
}

void finalize_stroke(void)
{
  if (ui.cur_path.num_points == 1) { // GnomeCanvas doesn't like num_points=1
    ui.cur_path.coords[2] = ui.cur_path.coords[0]+0.1;
    ui.cur_path.coords[3] = ui.cur_path.coords[1];
    ui.cur_path.num_points = 2;
    ui.cur_item->brush.variable_width = FALSE;
  }
  
  if (!ui.cur_item->brush.variable_width)
    subdivide_cur_path(); // split the segment so eraser will work

  ui.cur_item->path = gnome_canvas_points_new(ui.cur_path.num_points);
  g_memmove(ui.cur_item->path->coords, ui.cur_path.coords, 
      2*ui.cur_path.num_points*sizeof(double));
  if (ui.cur_item->brush.variable_width)
    ui.cur_item->widths = (gdouble *)g_memdup(ui.cur_widths, 
                            (ui.cur_path.num_points-1)*sizeof(gdouble));
  else ui.cur_item->widths = NULL;
  update_item_bbox(ui.cur_item);
  ui.cur_path.num_points = 0;

  if (!ui.cur_item->brush.variable_width) {
    // destroy the entire group of temporary line segments
    gtk_object_destroy(GTK_OBJECT(ui.cur_item->canvas_item));
    // make a new line item to replace it
    make_canvas_item_one(ui.cur_layer->group, ui.cur_item);
  }

  // add undo information
  prepare_new_undo();
  undo->type = ITEM_STROKE;
  undo->item = ui.cur_item;
  undo->layer = ui.cur_layer;

  // store the item on top of the layer stack
  ui.cur_layer->items = g_list_append(ui.cur_layer->items, ui.cur_item);
  ui.cur_layer->nitems++;
  ui.cur_item = NULL;
  ui.cur_item_type = ITEM_NONE;
}

/************** eraser tool *************/

void erase_stroke_portions(struct Item *item, double x, double y, double radius,
                   gboolean whole_strokes, struct UndoErasureData *erasure)
{
  int i;
  double *pt;
  struct Item *newhead, *newtail;
  gboolean need_recalc = FALSE;

  for (i=0, pt=item->path->coords; i<item->path->num_points; i++, pt+=2) {
    if (hypot(pt[0]-x, pt[1]-y) <= radius) { // found an intersection
      // FIXME: need to test if line SEGMENT hits the circle
      // hide the canvas item, and create erasure data if needed
      if (erasure == NULL) {
        item->type = ITEM_TEMP_STROKE;
        gnome_canvas_item_hide(item->canvas_item);  
            /*  we'll use this hidden item as an insertion point later */
        erasure = (struct UndoErasureData *)g_malloc(sizeof(struct UndoErasureData));
        item->erasure = erasure;
        erasure->item = item;
        erasure->npos = g_list_index(ui.cur_layer->items, item);
        erasure->nrepl = 0;
        erasure->replacement_items = NULL;
      }
      // split the stroke
      newhead = newtail = NULL;
      if (!whole_strokes) {
        if (i>=2) {
          newhead = (struct Item *)g_malloc(sizeof(struct Item));
          newhead->type = ITEM_STROKE;
          g_memmove(&newhead->brush, &item->brush, sizeof(struct Brush));
          newhead->path = gnome_canvas_points_new(i);
          g_memmove(newhead->path->coords, item->path->coords, 2*i*sizeof(double));
          if (newhead->brush.variable_width)
            newhead->widths = (gdouble *)g_memdup(item->widths, (i-1)*sizeof(gdouble));
          else newhead->widths = NULL;
        }
        while (++i < item->path->num_points) {
          pt+=2;
          if (hypot(pt[0]-x, pt[1]-y) > radius) break;
        }
        if (i<item->path->num_points-1) {
          newtail = (struct Item *)g_malloc(sizeof(struct Item));
          newtail->type = ITEM_STROKE;
          g_memmove(&newtail->brush, &item->brush, sizeof(struct Brush));
          newtail->path = gnome_canvas_points_new(item->path->num_points-i);
          g_memmove(newtail->path->coords, item->path->coords+2*i, 
                           2*(item->path->num_points-i)*sizeof(double));
          if (newtail->brush.variable_width)
            newtail->widths = (gdouble *)g_memdup(item->widths+i, 
              (item->path->num_points-i-1)*sizeof(gdouble));
          else newtail->widths = NULL;
          newtail->canvas_item = NULL;
        }
      }
      if (item->type == ITEM_STROKE) { 
        // it's inside an erasure list - we destroy it
        gnome_canvas_points_free(item->path);
        if (item->brush.variable_width) g_free(item->widths);
        if (item->canvas_item != NULL) 
          gtk_object_destroy(GTK_OBJECT(item->canvas_item));
        erasure->nrepl--;
        erasure->replacement_items = g_list_remove(erasure->replacement_items, item);
        g_free(item);
      }
      // add the new head
      if (newhead != NULL) {
        update_item_bbox(newhead);
        make_canvas_item_one(ui.cur_layer->group, newhead);
        lower_canvas_item_to(ui.cur_layer->group,
                  newhead->canvas_item, erasure->item->canvas_item);
        erasure->replacement_items = g_list_prepend(erasure->replacement_items, newhead);
        erasure->nrepl++;
        // prepending ensures it won't get processed twice
      }
      // recurse into the new tail
      need_recalc = (newtail!=NULL);
      if (newtail == NULL) break;
      item = newtail;
      erasure->replacement_items = g_list_prepend(erasure->replacement_items, newtail);
      erasure->nrepl++;
      i=0; pt=item->path->coords;
    }
  }
  // add the tail if needed
  if (!need_recalc) return;
  update_item_bbox(item);
  make_canvas_item_one(ui.cur_layer->group, item);
  lower_canvas_item_to(ui.cur_layer->group, item->canvas_item, 
                                      erasure->item->canvas_item);
}


void do_eraser(GdkEvent *event, double radius, gboolean whole_strokes)
{
  struct Item *item, *repl;
  GList *itemlist, *repllist;
  double pos[2];
  struct BBox eraserbox;
  
  get_pointer_coords(event, pos);
  eraserbox.left = pos[0]-radius;
  eraserbox.right = pos[0]+radius;
  eraserbox.top = pos[1]-radius;
  eraserbox.bottom = pos[1]+radius;
  for (itemlist = ui.cur_layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type == ITEM_STROKE) {
      if (!have_intersect(&(item->bbox), &eraserbox)) continue;
      erase_stroke_portions(item, pos[0], pos[1], radius, whole_strokes, NULL);
    } else if (item->type == ITEM_TEMP_STROKE) {
      repllist = item->erasure->replacement_items;
      while (repllist!=NULL) {
        repl = (struct Item *)repllist->data;
          // we may delete the item soon! so advance now in the list
        repllist = repllist->next; 
        if (have_intersect(&(repl->bbox), &eraserbox))
          erase_stroke_portions(repl, pos[0], pos[1], radius, whole_strokes, item->erasure);
      }
    }
  }
}

void finalize_erasure(void)
{
  GList *itemlist, *partlist;
  struct Item *item;
  
  prepare_new_undo();
  undo->type = ITEM_ERASURE;
  undo->layer = ui.cur_layer;
  undo->erasurelist = NULL;
  
  itemlist = ui.cur_layer->items;
  while (itemlist!=NULL) {
    item = (struct Item *)itemlist->data;
    itemlist = itemlist->next;
    if (item->type != ITEM_TEMP_STROKE) continue;
    item->type = ITEM_STROKE;
    ui.cur_layer->items = g_list_remove(ui.cur_layer->items, item);
    // the item has an invisible canvas item, which used to act as anchor
    if (item->canvas_item!=NULL) {
      gtk_object_destroy(GTK_OBJECT(item->canvas_item));
      item->canvas_item = NULL;
    }
    undo->erasurelist = g_list_append(undo->erasurelist, item->erasure);
    // add the new strokes into the current layer
    for (partlist = item->erasure->replacement_items; partlist!=NULL; partlist = partlist->next)
      ui.cur_layer->items = g_list_insert_before(
                      ui.cur_layer->items, itemlist, partlist->data);
    ui.cur_layer->nitems += item->erasure->nrepl-1;
  }
    
  ui.cur_item = NULL;
  ui.cur_item_type = ITEM_NONE;
  
  /* NOTE: the list of erasures goes in the depth order of the layer;
     this guarantees that, upon undo, the erasure->npos fields give the
     correct position where each item should be reinserted as the list
     is traversed in the forward direction */
}

/************ selection tools ***********/

void make_dashed(GnomeCanvasItem *item)
{
  double dashlen[2];
  ArtVpathDash dash;
  
  dash.n_dash = 2;
  dash.offset = 3.0;
  dash.dash = dashlen;
  dashlen[0] = dashlen[1] = 6.0;
  gnome_canvas_item_set(item, "dash", &dash, NULL);
}


gboolean item_within_selection(struct Item *item, struct SelectionContext *sc) 
{
  if (!sc) return FALSE;
  switch (sc->type) {
  case ITEM_SELECTREGION:
    return (hittest_item(sc->lassosvp, item));
  case ITEM_SELECTRECT:
    return (item->bbox.left >= sc->x1 && item->bbox.right <= sc->x2 &&
	    item->bbox.top >= sc->y1 && item->bbox.bottom <= sc->y2);
  default:
    return (FALSE);
  }
}


void free_selection_context(struct SelectionContext *sc) 
{
  if (sc->lassosvp) art_svp_free(sc->lassosvp); 
}

void get_selection_context_rect(struct SelectionContext *sc) 
{
  if (ui.selection->bbox.left > ui.selection->bbox.right) {
    sc->x1 = ui.selection->bbox.right;  sc->x2 = ui.selection->bbox.left;
    ui.selection->bbox.left = sc->x1;   ui.selection->bbox.right = sc->x2;
  } else {
    sc->x1 = ui.selection->bbox.left;  sc->x2 = ui.selection->bbox.right;
  }
  if (ui.selection->bbox.top > ui.selection->bbox.bottom) {
    sc->y1 = ui.selection->bbox.bottom;  sc->y2 = ui.selection->bbox.top;
    ui.selection->bbox.top = sc->y1;   ui.selection->bbox.bottom = sc->y2;
  } else {
    sc->y1 = ui.selection->bbox.top;  sc->y2 = ui.selection->bbox.bottom;
  }
}

void get_selection_context_lasso(struct SelectionContext *sc) 
{
  ArtBpath *bpath; 
  ArtVpath *vpath; 
  ArtDRect lassosvp_bbox;
  bpath = gnome_canvas_path_def_bpath(ui.selection->closedlassopath); 
  vpath = art_bez_path_to_vec(bpath, 0.25); 
  sc->lassosvp = art_svp_from_vpath(vpath); 
  art_free(vpath);
  art_drect_svp(&lassosvp_bbox, sc->lassosvp);
  sc->x1 = lassosvp_bbox.x0; sc->x2 = lassosvp_bbox.x1; 
  sc->y1 = lassosvp_bbox.y0; sc->y2 = lassosvp_bbox.y1; 
}

void get_selection_context(int selection_type, struct SelectionContext *sc) 
{
  sc->lassosvp = NULL;
  switch (selection_type) {
  case ITEM_SELECTREGION:
    sc->type = ITEM_SELECTREGION;
    get_selection_context_lasso(sc);
    break;
  case ITEM_SELECTRECT:
    sc->type = ITEM_SELECTRECT;
    get_selection_context_rect(sc);
    break;
  default:
    sc = NULL;
    break;
  }
}

/* Allocates new selection */
void get_new_selection(int selection_type, struct Layer *layer)
{
  ui.selection = g_new(struct Selection, 1);
  ui.selection->type = selection_type;
  ui.selection->layer = layer;
  ui.selection->items = NULL;
  ui.selection->canvas_item = NULL;
  ui.selection->move_layer = NULL;
  ui.selection->lasso = NULL; 
  ui.selection->lassopath = NULL; 
  ui.selection->closedlassopath = NULL; 
}

void start_selectregion(GdkEvent *event)
{
  double pt[2];
  get_pointer_coords(event, pt); 
  ui.cur_item_type = ITEM_SELECTREGION;
  reset_selection();
  get_new_selection(ITEM_SELECTREGION, ui.cur_layer);
  
  ui.selection->lassopath = gnome_canvas_path_def_new(); 
  gnome_canvas_path_def_moveto(ui.selection->lassopath, pt[0], pt[1]); 
  ui.selection->closedlassopath = gnome_canvas_path_def_close_all(ui.selection->lassopath);
  ui.selection->lasso = (GnomeCanvasBpath*)canvas_item_new_for_selection(ITEM_SELECTREGION);
  make_dashed((GnomeCanvasItem*)ui.selection->lasso); 

  ui.selection->bbox.left = ui.selection->bbox.right = pt[0];
  ui.selection->bbox.top = ui.selection->bbox.bottom = pt[1];
  ui.selection->canvas_item = canvas_item_new_for_selection(ITEM_SELECTRECT);
  update_cursor();
}
 
void start_selectrect(GdkEvent *event)
{
  double pt[2];
  reset_selection();
  
  ui.cur_item_type = ITEM_SELECTRECT;
  get_new_selection(ITEM_SELECTRECT, ui.cur_layer);

  get_pointer_coords(event, pt);
  ui.selection->bbox.left = ui.selection->bbox.right = pt[0];
  ui.selection->bbox.top = ui.selection->bbox.bottom = pt[1];
 
  ui.selection->canvas_item = canvas_item_new_for_selection(ITEM_SELECTRECT);
  update_cursor();
}

void finalize_selectregion() { finalize_selection(ITEM_SELECTREGION); }
void finalize_selectrect() { finalize_selection(ITEM_SELECTRECT); }

void populate_selection(struct SelectionContext *sc) 
{
  GList *itemlist;
  struct Item *item;
  double xpadding, ypadding, minwidth, minheight, w, h;
  minwidth = MIN_SEL_SCALE*ui.screen_width;
  minheight = MIN_SEL_SCALE*ui.screen_height;

  for (itemlist = ui.selection->layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item_within_selection(item, sc)) {
      if(g_list_length(ui.selection->items) == 0)
	ui.selection->bbox = item->bbox;
      else 
	ui.selection->bbox = bboxadd(ui.selection->bbox, item->bbox); 

      ui.selection->items = g_list_append(ui.selection->items, item); 

      w = bbox_width(ui.selection->bbox); h = bbox_height(ui.selection->bbox);
      xpadding = w < minwidth ? (minwidth - w) / 2 : DEFAULT_PADDING;
      ypadding = h < minheight ? (minheight - h) / 2 : DEFAULT_PADDING;
    }
  }
  bbox_pad_symm(&ui.selection->bbox, xpadding, ypadding);
}

void select_object_maybe(struct SelectionContext *sc)
{
  struct Item *item;
  if (ui.selection->items == NULL) { // perhaps we are selecting an object
    item = click_is_in_object(ui.selection->layer, sc->x1, sc->y1);
    if (item != NULL && item == click_is_in_object(ui.selection->layer, sc->x2, sc->y2)) {
      ui.selection->items = g_list_append(ui.selection->items, item);
      make_bbox_copy(&(ui.selection->bbox), &(item->bbox), DEFAULT_PADDING);
    }
  }
}

void render_selection_marquee(struct SelectionContext *sc) 
{
  if (ui.selection->items == NULL) 
    reset_selection();
  else {
    // hide the temporary lasso
    if (sc->type == ITEM_SELECTREGION)
      gnome_canvas_item_hide((GnomeCanvasItem*) ui.selection->lasso);
    gnome_canvas_item_set(ui.selection->canvas_item,
			  "x1", ui.selection->bbox.left, "x2", ui.selection->bbox.right, 
			  "y1", ui.selection->bbox.top, "y2", ui.selection->bbox.bottom, NULL);
    make_dashed(ui.selection->canvas_item);
  }
}

void finalize_selection(int selection_type)
{
  SelectionContext sc;
  get_selection_context(selection_type, &sc);

  ui.cur_item_type = ITEM_NONE;
  populate_selection(&sc);
  select_object_maybe(&sc);
  render_selection_marquee(&sc);
  
  free_selection_context(&sc);
  update_cursor();
  update_copy_paste_enabled();
  update_font_button();
}

gboolean item_under_point(struct Item *item, double x, double y)
{
  return(x > item->bbox.left && x < item->bbox.right &&
	 y > item->bbox.top && y < item->bbox.bottom);
}


gboolean start_movesel(GdkEvent *event)
{
  double pt[2];
  int mapping;
  mapping = get_mapping((GdkEventButton *)event);

  if (ui.selection==NULL) return FALSE;
  if (ui.cur_layer != ui.selection->layer) return FALSE;
  
  get_pointer_coords(event, pt);
  if (ui.selection->type == ITEM_SELECTRECT || ui.selection->type == ITEM_SELECTREGION ) {
    if (pt[0]<ui.selection->bbox.left || pt[0]>ui.selection->bbox.right ||
        pt[1]<ui.selection->bbox.top  || pt[1]>ui.selection->bbox.bottom)
      return FALSE;
    if (get_mapping((GdkEventButton *)event) == COPY_SEL_MAPPING) {
      selection_to_clip();
      clipboard_paste_with_offset(TRUE, 1,1);
    } 
    ui.cur_item_type = ITEM_MOVESEL;
    ui.selection->anchor_x = ui.selection->last_x = pt[0];
    ui.selection->anchor_y = ui.selection->last_y = pt[1];
    ui.selection->orig_pageno = ui.pageno;
    ui.selection->move_pageno = ui.pageno;
    ui.selection->move_layer = ui.selection->layer;
    ui.selection->move_pagedelta = 0.;
    gnome_canvas_item_set(ui.selection->canvas_item, "dash", NULL, NULL);
    update_cursor();
    return TRUE;
  }
  return FALSE;
}

gboolean start_resizesel(GdkEvent *event)
{
  double pt[2];
  gboolean can_resize_left, can_resize_right, can_resize_bottom, can_resize_top;

  if (ui.selection==NULL) return FALSE;
  if (ui.cur_layer != ui.selection->layer) return FALSE;
  get_pointer_coords(event, pt);

  if (ui.selection->type == ITEM_SELECTRECT || ui.selection->type == ITEM_SELECTREGION ) {
     get_possible_resize_direction(pt, &can_resize_left, &can_resize_right, &can_resize_top, &can_resize_bottom);
     if (!(can_resize_left || can_resize_right || can_resize_bottom || can_resize_top))
       return FALSE;

    // now, if the click is near the edge, it's a resize operation
    // keep track of which edges we're close to, since those are the ones which should move
     ui.selection->resizing_left = can_resize_left;
     ui.selection->resizing_right = can_resize_right;
     ui.selection->resizing_bottom = can_resize_bottom;
     ui.selection->resizing_top = can_resize_top;

    // fix aspect ratio if we are near a corner; corner IDs:
    // 00 LL, 01 UL, 10 LR, 11 UR
    if ((ui.selection->resizing_left || ui.selection->resizing_right) &&
	(ui.selection->resizing_top  || ui.selection->resizing_bottom)) {
      ui.selection->fix_aspect_ratio = TRUE;
      ui.selection->aspect_ratio = fabs((ui.selection->bbox.right - ui.selection->bbox.left) / 
					(ui.selection->bbox.top - ui.selection->bbox.bottom));
      if (ui.selection->resizing_left && ui.selection->resizing_bottom)
	ui.selection->corner_id = 00;
      else if (ui.selection->resizing_left && ui.selection->resizing_top)
	ui.selection->corner_id = 01;
      else if (ui.selection->resizing_right && ui.selection->resizing_top)
	ui.selection->corner_id = 11;
      else
	ui.selection->corner_id = 10;
    } else 
      ui.selection->fix_aspect_ratio = FALSE;


    ui.cur_item_type = ITEM_RESIZESEL;
    ui.selection->new_y1 = ui.selection->bbox.top;
    ui.selection->new_y2 = ui.selection->bbox.bottom;
    ui.selection->new_x1 = ui.selection->bbox.left;
    ui.selection->new_x2 = ui.selection->bbox.right;
    gnome_canvas_item_set(ui.selection->canvas_item, "dash", NULL, NULL);
    update_cursor_for_resize(pt);
    return TRUE;
  }
  return FALSE;
} 



void start_vertspace(GdkEvent *event)
{
  double pt[2];
  GList *itemlist;
  struct Item *item;

  reset_selection();
  ui.cur_item_type = ITEM_MOVESEL_VERT;
  get_new_selection(ITEM_MOVESEL_VERT, ui.cur_layer);

  get_pointer_coords(event, pt);
  ui.selection->bbox.top = ui.selection->bbox.bottom = pt[1];
  for (itemlist = ui.cur_layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->bbox.top >= pt[1]) {
      ui.selection->items = g_list_append(ui.selection->items, item); 
      if (item->bbox.bottom > ui.selection->bbox.bottom)
        ui.selection->bbox.bottom = item->bbox.bottom;
    }
  }

  ui.selection->anchor_x = ui.selection->last_x = 0;
  ui.selection->anchor_y = ui.selection->last_y = pt[1];
  ui.selection->orig_pageno = ui.pageno;
  ui.selection->move_pageno = ui.pageno;
  ui.selection->move_layer = ui.selection->layer;
  ui.selection->move_pagedelta = 0.;
  ui.selection->canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
      gnome_canvas_rect_get_type(), "width-pixels", 1, 
      "outline-color-rgba", 0x000000ff,
      "fill-color-rgba", 0x80808040,
      "x1", -100.0, "x2", ui.cur_page->width+100, "y1", pt[1], "y2", pt[1], NULL);
  update_cursor();
}

void continue_movesel(GdkEvent *event)
{
  double pt[2], dx, dy, upmargin;
  GList *list;
  struct Item *item;
  int tmppageno;
  struct Page *tmppage;
  

  get_pointer_coords(event, pt);
  if (ui.cur_item_type == ITEM_MOVESEL_VERT) pt[0] = 0;
  pt[1] += ui.selection->move_pagedelta;

  // check for page jumps
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    upmargin = ui.selection->bbox.bottom - ui.selection->bbox.top;
  else upmargin = VIEW_CONTINUOUS_SKIP;
  tmppageno = ui.selection->move_pageno;
  tmppage = g_list_nth_data(journal.pages, tmppageno);
  while (ui.view_continuous && (pt[1] < - upmargin)) {
    if (tmppageno == 0) break;
    tmppageno--;
    tmppage = g_list_nth_data(journal.pages, tmppageno);
    pt[1] += tmppage->height + VIEW_CONTINUOUS_SKIP;
    ui.selection->move_pagedelta += tmppage->height + VIEW_CONTINUOUS_SKIP;
  }
  while (ui.view_continuous && (pt[1] > tmppage->height+VIEW_CONTINUOUS_SKIP)) {
    if (tmppageno == journal.npages-1) break;
    pt[1] -= tmppage->height + VIEW_CONTINUOUS_SKIP;
    ui.selection->move_pagedelta -= tmppage->height + VIEW_CONTINUOUS_SKIP;
    tmppageno++;
    tmppage = g_list_nth_data(journal.pages, tmppageno);
  }
  
  if (tmppageno != ui.selection->move_pageno) {
    // move to a new page !
    ui.selection->move_pageno = tmppageno;
    if (tmppageno == ui.selection->orig_pageno)
      ui.selection->move_layer = ui.selection->layer;
    else
      ui.selection->move_layer = (struct Layer *)(g_list_last(
        ((struct Page *)g_list_nth_data(journal.pages, tmppageno))->layers)->data);
    gnome_canvas_item_reparent(ui.selection->canvas_item, ui.selection->move_layer->group);
    for (list = ui.selection->items; list!=NULL; list = list->next) {
      item = (struct Item *)list->data;
      if (item->canvas_item!=NULL)
        gnome_canvas_item_reparent(item->canvas_item, ui.selection->move_layer->group);
    }
    // avoid a refresh bug
    gnome_canvas_item_move(GNOME_CANVAS_ITEM(ui.selection->move_layer->group), 0., 0.);
    if (ui.cur_item_type == ITEM_MOVESEL_VERT)
      gnome_canvas_item_set(ui.selection->canvas_item,
        "x2", tmppage->width+100, 
        "y1", ui.selection->anchor_y+ui.selection->move_pagedelta, NULL);
  }
  
  // now, process things normally

  dx = pt[0] - ui.selection->last_x;
  dy = pt[1] - ui.selection->last_y;
  if (hypot(dx,dy) < 1) return; // don't move subpixel
  ui.selection->last_x = pt[0];
  ui.selection->last_y = pt[1];

  // move the canvas items
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    gnome_canvas_item_set(ui.selection->canvas_item, "y2", pt[1], NULL);
  else 
    gnome_canvas_item_move(ui.selection->canvas_item, dx, dy);
  
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    if (item->canvas_item != NULL)
      gnome_canvas_item_move(item->canvas_item, dx, dy);
  }
}

void continue_resizesel(GdkEvent *event)
{
  double pt[2];
  double old_ar;
  int new_width, old_width, new_height, old_height, tmp;

  get_pointer_coords(event, pt);
  old_ar = ui.selection->aspect_ratio;
  old_width = (int)fabs(ui.selection->bbox.right - ui.selection->bbox.left);
  old_height = (int)fabs(ui.selection->bbox.top - ui.selection->bbox.bottom);
  new_height = old_height;
  new_width = old_width;

  if (ui.selection->resizing_top) {
    ui.selection->new_y1 = pt[1];
    new_height = ui.selection->bbox.bottom - pt[1];
      /* printf("RESIZE TOP, OW=%d, NW=%d, OH=%d, NH=%d\n",old_width,new_width,old_height,new_height); */
  }
  if (ui.selection->resizing_bottom){
    ui.selection->new_y2 = pt[1];
    new_height = - (ui.selection->bbox.top - pt[1]);
  } 
  if (ui.selection->resizing_left) {
    ui.selection->new_x1 = pt[0];
    new_width = ui.selection->bbox.right - pt[0];
  }
  if (ui.selection->resizing_right) {
    ui.selection->new_x2 = pt[0];
    new_width = - (ui.selection->bbox.left - pt[0]);
    /* printf("RESIZE RIGHT, OW=%d, NW=%d, OH=%d, NH=%d\n",old_width,new_width,old_height,new_height); */
  }
  
  tmp = new_height;
  new_height = (int) new_width / old_ar;
  new_width = (int) tmp * old_ar;

  //  if AR >=1 control x, match y, else control y, match x
  if (ui.selection->fix_aspect_ratio)
    switch (ui.selection->corner_id) { // 00 LL, 01 UL, 10 LR, 11 UR
    case 00:
      if (old_ar >= 1) //recompute y2
  	ui.selection->new_y2 = ui.selection->bbox.top + new_height;
      else //recompute x1
  	ui.selection->new_x1 = ui.selection->bbox.right - new_width;
      break;
    case 01:
      if (old_ar >= 1) //recompute y1
  	ui.selection->new_y1 = ui.selection->bbox.bottom - new_height;
      else //recompute x1
  	ui.selection->new_x1 = ui.selection->bbox.right - new_width;
      break;
    case 10:
      if (old_ar >= 1) //recompute y2
  	ui.selection->new_y2 = ui.selection->bbox.top + new_height;
      else //recompute x2
  	ui.selection->new_x2 = ui.selection->bbox.left + new_width;
      break;
    case 11:
      if (old_ar >= 1) //recompute y1
  	ui.selection->new_y1 = ui.selection->bbox.bottom - new_height;
      else //recompute x2
  	ui.selection->new_x2 = ui.selection->bbox.left + new_width;
      break;
    }

  gnome_canvas_item_set(ui.selection->canvas_item, 
    "x1", ui.selection->new_x1, "x2", ui.selection->new_x2,
    "y1", ui.selection->new_y1, "y2", ui.selection->new_y2, NULL);
}

void finalize_movesel(void)
{
  GList *list, *link;
  
  if (ui.selection->items != NULL) {
    prepare_new_undo();
    undo->type = ITEM_MOVESEL;
    undo->itemlist = g_list_copy(ui.selection->items);
    undo->val_x = ui.selection->last_x - ui.selection->anchor_x;
    undo->val_y = ui.selection->last_y - ui.selection->anchor_y;
    undo->layer = ui.selection->layer;
    undo->layer2 = ui.selection->move_layer;
    undo->auxlist = NULL;
    // build auxlist = pointers to Item's just before ours (for depths)
    for (list = ui.selection->items; list!=NULL; list = list->next) {
      link = g_list_find(ui.selection->layer->items, list->data);
      if (link!=NULL) link = link->prev;
      undo->auxlist = g_list_append(undo->auxlist, ((link!=NULL) ? link->data : NULL));
    }
    ui.selection->layer = ui.selection->move_layer;
    move_journal_items_by(undo->itemlist, undo->val_x, undo->val_y,
                          undo->layer, undo->layer2, 
                          (undo->layer == undo->layer2)?undo->auxlist:NULL);
  }

  if (ui.selection->move_pageno!=ui.selection->orig_pageno) 
    do_switch_page(ui.selection->move_pageno, FALSE, FALSE);
    
  if (ui.cur_item_type == ITEM_MOVESEL_VERT)
    reset_selection();
  else {
    ui.selection->bbox.left += undo->val_x;
    ui.selection->bbox.right += undo->val_x;
    ui.selection->bbox.top += undo->val_y;
    ui.selection->bbox.bottom += undo->val_y;
    make_dashed(ui.selection->canvas_item);
    /* update selection box object's offset to be trivial, and its internal 
       coordinates to agree with those of the bbox; need this since resize
       operations will modify the box by setting its coordinates directly */
    gnome_canvas_item_affine_absolute(ui.selection->canvas_item, NULL);
    gnome_canvas_item_set(ui.selection->canvas_item, 
      "x1", ui.selection->bbox.left, "x2", ui.selection->bbox.right,
      "y1", ui.selection->bbox.top, "y2", ui.selection->bbox.bottom, NULL);
  }
  ui.cur_item_type = ITEM_NONE;
  update_cursor();
}

#define SCALING_EPSILON 0.001

void finalize_resizesel(void)
{
  struct Item *item;

  // build the affine transformation
  double offset_x, offset_y, scaling_x, scaling_y;
  scaling_x = (ui.selection->new_x2 - ui.selection->new_x1) / 
              (ui.selection->bbox.right - ui.selection->bbox.left);
  scaling_y = (ui.selection->new_y2 - ui.selection->new_y1) /
              (ui.selection->bbox.bottom - ui.selection->bbox.top);
  // couldn't undo a resize-by-zero...
  if (fabs(scaling_x)<SCALING_EPSILON) scaling_x = SCALING_EPSILON;
  if (fabs(scaling_y)<SCALING_EPSILON) scaling_y = SCALING_EPSILON;
  offset_x = ui.selection->new_x1 - ui.selection->bbox.left * scaling_x;
  offset_y = ui.selection->new_y1 - ui.selection->bbox.top * scaling_y;

  if (ui.selection->items != NULL) {
    // create the undo information
    prepare_new_undo();
    undo->type = ITEM_RESIZESEL;
    undo->itemlist = g_list_copy(ui.selection->items);
    undo->auxlist = NULL;

    undo->scaling_x = scaling_x;
    undo->scaling_y = scaling_y;
    undo->val_x = offset_x;
    undo->val_y = offset_y;

    // actually do the resize operation
    resize_journal_items_by(ui.selection->items, scaling_x, scaling_y, offset_x, offset_y);
  }

  if (scaling_x>0) {
    ui.selection->bbox.left = ui.selection->new_x1;
    ui.selection->bbox.right = ui.selection->new_x2;
  } else {
    ui.selection->bbox.left = ui.selection->new_x2;
    ui.selection->bbox.right = ui.selection->new_x1;
  }
  if (scaling_y>0) {
    ui.selection->bbox.top = ui.selection->new_y1;
    ui.selection->bbox.bottom = ui.selection->new_y2;
  } else {
    ui.selection->bbox.top = ui.selection->new_y2;
    ui.selection->bbox.bottom = ui.selection->new_y1;
  }
  make_dashed(ui.selection->canvas_item);

  ui.cur_item_type = ITEM_NONE;
  update_cursor();
}

void selection_delete(void)
{
  struct UndoErasureData *erasure;
  GList *itemlist;
  struct Item *item;
  
  if (ui.selection == NULL) return;
  prepare_new_undo();
  undo->type = ITEM_ERASURE;
  undo->layer = ui.selection->layer;
  undo->erasurelist = NULL;
  for (itemlist = ui.selection->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->canvas_item!=NULL)
      gtk_object_destroy(GTK_OBJECT(item->canvas_item));
    erasure = g_new(struct UndoErasureData, 1);
    erasure->item = item;
    erasure->npos = g_list_index(ui.selection->layer->items, item);
    erasure->nrepl = 0;
    erasure->replacement_items = NULL;
    ui.selection->layer->items = g_list_remove(ui.selection->layer->items, item);
    ui.selection->layer->nitems--;
    undo->erasurelist = g_list_prepend(undo->erasurelist, erasure);
  }
  reset_selection();

  /* NOTE: the erasurelist is built backwards; this guarantees that,
     upon undo, the erasure->npos fields give the correct position
     where each item should be reinserted as the list is traversed in
     the forward direction */
}

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
  unsigned char *p;
  int nitems, nimages;
  GdkPixbuf *imref;
  struct Item *item;
  p = (unsigned char *)user_data + sizeof(int); //skip bufsize
  g_memmove(&nitems, p, sizeof(int)); p+= sizeof(int); //skip nitems
  g_memmove(&nimages, p, sizeof(int)); p+= sizeof(int);
  while(nimages-- > 0) {
    g_memmove(&imref, p, sizeof(GdkPixbuf *));
    g_object_unref(imref); p+= sizeof(GdkPixbuf *); //unref item->image
    g_memmove(&imref, p, sizeof(GdkPixbuf *));
    g_object_unref(imref); p+= sizeof(GdkPixbuf *); //unref item->image_scaled
  }
  g_free(user_data);
}

void copy_to_buffer_advance_ptr(unsigned char **to, gpointer from, gsize size)
{
  g_memmove(*to, from, size); *to+=size;
}

void copy_from_buffer_advance_ptr(gpointer to, unsigned char **from, gsize size)
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
      + sizeof(gboolean)
      + 4*sizeof(double) // bbox 
      + sizeof(unsigned int)
      + sizeof(GdkPixbuf*)
      + sizeof(GdkPixbuf*);
    break;
  default: // will just return sizeof(int) for type field storage
    break;
  }
  return bufsz;
}

// buffer layout:
// bufsz, nitems, nimages, [img,img_sc]*, bbox, [item]*
// everything before the item entries is considered a header
// the [img,img_sc] pointer pairs are for proper reference counting
int buffer_size_for_header(int nimages)
{
  int bufsz = 0;
  int img_ptr_store_size = 2*nimages*sizeof(GdkPixbuf *);
  bufsz = 2*sizeof(int) // bufsz, nitems
    + sizeof(struct BBox) // bbox
    + sizeof(int) // nimages
    + img_ptr_store_size; // store image pointers up front for easy unreferencing
  return bufsz;
}

void put_item_in_buffer(struct Item *item, unsigned char **pp)
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
    copy_to_buffer_advance_ptr(pp, &item->image, sizeof(GdkPixbuf*));
    g_object_ref(item->image); 
    copy_to_buffer_advance_ptr(pp, &item->image_scaled, sizeof(GdkPixbuf*));
    g_object_ref(item->image_scaled);
    break;
  default:
    break;
  }
}

void selection_to_clip(void) 
{
  int bufsz, nitems, nimages, val, img_ptr_store_size;
  unsigned char *buf, *p, *p_imrefs;
  GList *list;
  struct Item *item;
  GtkTargetEntry target;
  
  if (ui.selection == NULL) return;
  bufsz = 0; nitems = 0; nimages = 0;
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    nitems++;
    if (item->type == ITEM_IMAGE) nimages++;
    bufsz += buffer_size_for_item(item);
  }
  img_ptr_store_size = 2*nimages*sizeof(GdkPixbuf *);
  bufsz += buffer_size_for_header(nimages); // size of header, incl. image
					    // pointers which we store up
					    // front for easy unreferencing
  p = buf = g_malloc(bufsz);
  g_memmove(p, &bufsz, sizeof(int)); p+= sizeof(int);
  g_memmove(p, &nitems, sizeof(int)); p+= sizeof(int);
  g_memmove(p, &nimages, sizeof(int)); p+= sizeof(int);
  // image pointers are kept in a region before the item storage
  // These need to be decremented when the buffer is
  // destroyed, else we have a memory leak.
  // For every image, we store two pointers one after another (image and image_scaled)
  p_imrefs = p;
  p+= img_ptr_store_size;
  // bbox also goes before the item storage
  g_memmove(p, &ui.selection->bbox, sizeof(struct BBox)); p+= sizeof(struct BBox);
  for (list = ui.selection->items; list != NULL; list = list->next) {
    item = (struct Item *)list->data;
    put_item_in_buffer(item, &p);
    if (item->type == ITEM_IMAGE) {
      g_memmove(p_imrefs, &item->image, sizeof(GdkPixbuf*)); p_imrefs+= sizeof(GdkPixbuf*);
      g_memmove(p_imrefs, &item->image_scaled, sizeof(GdkPixbuf*)); p_imrefs+= sizeof(GdkPixbuf*);
    }
  }

  target.target = "_XOURNAL";
  target.flags = 0;
  target.info = 0;
  
  gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), 
       &target, 1,
       callback_clipboard_get, callback_clipboard_clear, buf);
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


void import_img_as_clipped_item()
{
  int bufsz, nitems = 1, nimages = 1;
  unsigned char *buf, *p;
  GtkTargetEntry target;
  GdkPixbuf *pixbuf;
  struct Item *item;
  struct BBox selection_bbox;
  double scale=1;
  char *paste_fname_base = "paste_";
  int IMG_INDEX_MAX_SIZE = 11;
  /* printf("wait for image...."); */
  pixbuf = gtk_clipboard_wait_for_image(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
  /* printf("got image..."); */
  if(pixbuf==NULL){
    /* open failed */
    ui.cur_item = NULL;
    ui.cur_item_type = ITEM_NONE;
    return;
  }
  item = g_new(struct Item, 1);
  item->type = ITEM_IMAGE;
  item->image_pasted = TRUE;
  item->image_id = journal.image_id_counter++;
  item->image_path = g_malloc(strlen(paste_fname_base) + IMG_INDEX_MAX_SIZE * sizeof(char));
  strcpy(item->image_path, paste_fname_base);
  sprintf(&(item->image_path[strlen(paste_fname_base)]),"%d",item->image_id);
  item->canvas_item = NULL;

  item->bbox.left = 0;
  item->bbox.top = 0;
  item->image = pixbuf;
    /* gdk_pixbuf_save(item->image,"/tmp/testpixbuf1","png", &error, NULL); */
  if(1>(ui.cur_page->width-item->bbox.left)/gdk_pixbuf_get_width(item->image)) //set scale so that it does not extend too far to the right
    scale=(ui.cur_page->width-item->bbox.left)/gdk_pixbuf_get_width(item->image);
  if(scale>(ui.cur_page->height-item->bbox.top)/gdk_pixbuf_get_height(item->image)) //set scale so that it does not extend too far to the bottom
    scale=(ui.cur_page->height-item->bbox.top)/gdk_pixbuf_get_height(item->image);
  /* gdk_pixbuf_save(item->image,"/tmp/testpixbuf2","png", &error, NULL); */
  if(scale == 1)
    item->image_scaled=gdk_pixbuf_copy(item->image);
  else
    item->image_scaled=gdk_pixbuf_scale_simple(item->image,
					       scale*gdk_pixbuf_get_width(item->image),
					       scale*gdk_pixbuf_get_height(item->image),
					       GDK_INTERP_NEAREST);
  /* gdk_pixbuf_save(item->image_scaled,"/tmp/testpixbuf3","png", &error, NULL); */

  item->bbox.right = item->bbox.left + gdk_pixbuf_get_width(item->image_scaled);
  item->bbox.bottom = item->bbox.top + gdk_pixbuf_get_height(item->image_scaled);
  g_memmove(&(item->brush), ui.cur_brush, sizeof(struct Brush));
  /* ui.cur_layer->items = g_list_append(ui.cur_layer->items, item); */
  /* ui.cur_layer->nitems++; */

  make_bbox_copy(&selection_bbox, &item->bbox, DEFAULT_PADDING);
  // now we put this item in a buffer in the same way that a selection item
  // would be processed on copy
  bufsz = buffer_size_for_header(nimages); // size of header, incl. image
  bufsz += buffer_size_for_item(item);
  p = buf = g_malloc(bufsz);
  copy_to_buffer_advance_ptr(&p, &bufsz, sizeof(int));
  copy_to_buffer_advance_ptr(&p, &nitems, sizeof(int));
  copy_to_buffer_advance_ptr(&p, &nimages, sizeof(int));
  copy_to_buffer_advance_ptr(&p, &item->image, sizeof(GdkPixbuf *));
  copy_to_buffer_advance_ptr(&p, &item->image_scaled, sizeof(GdkPixbuf *));
  copy_to_buffer_advance_ptr(&p, &selection_bbox, sizeof(struct BBox)); 
  put_item_in_buffer(item, &p);
  g_free(item);
   
   // finish setting this up as a regular selection
  target.target = "_XOURNAL";
  target.flags = 0;
  target.info = 0;
  
  gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), 
			      &target, 1,
			      callback_clipboard_get, callback_clipboard_clear, buf);

}


void get_item_from_buffer(struct Item *item, unsigned char **pp, double hoffset, double voffset)
{
  int i, npts, len;
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
    copy_from_buffer_advance_ptr(&item->image, pp, sizeof(GdkPixbuf*));
    copy_from_buffer_advance_ptr(&item->image_scaled, pp, sizeof(GdkPixbuf*));
      
    // copied image has the same underlying "image" (referenced twice), but
    // image_scaled is copied into its own storage
    tmp_pixbuf_ptr = gdk_pixbuf_copy(item->image_scaled);
    g_object_unref(item->image_scaled);
    item->image_scaled = tmp_pixbuf_ptr;
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
  int nitems, nimages, npts, i, len, im_refs, im_sc_refs;
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
  copy_from_buffer_advance_ptr(&nimages, &p, sizeof(int));
  p += 2*nimages*sizeof(GdkPixbuf *); // ignore the image pointers here
				      // (adjust refcount when retrieving
				      // images from buffer)
  
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
  update_cursor(); // FIYME: can't know if pointer is within selection!
}

// modify the color or thickness of pen strokes in a selection

void recolor_selection(int color_no, guint color_rgba)
{
  GList *itemlist;
  struct Item *item;
  struct Brush *brush;
  GnomeCanvasGroup *group;
  
  if (ui.selection == NULL) return;
  prepare_new_undo();
  undo->type = ITEM_REPAINTSEL;
  undo->itemlist = NULL;
  undo->auxlist = NULL;
  for (itemlist = ui.selection->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type != ITEM_STROKE && item->type != ITEM_TEXT) continue;
    if (item->type == ITEM_STROKE && item->brush.tool_type!=TOOL_PEN) continue;
    // store info for undo
    undo->itemlist = g_list_append(undo->itemlist, item);
    brush = (struct Brush *)g_malloc(sizeof(struct Brush));
    g_memmove(brush, &(item->brush), sizeof(struct Brush));
    undo->auxlist = g_list_append(undo->auxlist, brush);
    // repaint the stroke
    item->brush.color_no = color_no;
    item->brush.color_rgba = color_rgba | 0xff; // no alpha
    if (item->canvas_item!=NULL) {
      if (!item->brush.variable_width)
        gnome_canvas_item_set(item->canvas_item, 
           "fill-color-rgba", item->brush.color_rgba, NULL);
      else {
        group = (GnomeCanvasGroup *) item->canvas_item->parent;
        gtk_object_destroy(GTK_OBJECT(item->canvas_item));
        make_canvas_item_one(group, item);
      }
    }
  }
}

void rethicken_selection(int val)
{
  GList *itemlist;
  struct Item *item;
  struct Brush *brush;
  GnomeCanvasGroup *group;
  
  if (ui.selection == NULL) return;
  prepare_new_undo();
  undo->type = ITEM_REPAINTSEL;
  undo->itemlist = NULL;
  undo->auxlist = NULL;
  for (itemlist = ui.selection->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item->type != ITEM_STROKE || item->brush.tool_type!=TOOL_PEN) continue;
    // store info for undo
    undo->itemlist = g_list_append(undo->itemlist, item);
    brush = (struct Brush *)g_malloc(sizeof(struct Brush));
    g_memmove(brush, &(item->brush), sizeof(struct Brush));
    undo->auxlist = g_list_append(undo->auxlist, brush);
    // repaint the stroke
    item->brush.thickness_no = val;
    item->brush.thickness = predef_thickness[TOOL_PEN][val];
    if (item->canvas_item!=NULL) {
      if (!item->brush.variable_width)
        gnome_canvas_item_set(item->canvas_item, 
           "width-units", item->brush.thickness, NULL);
      else {
        group = (GnomeCanvasGroup *) item->canvas_item->parent;
        gtk_object_destroy(GTK_OBJECT(item->canvas_item));
        item->brush.variable_width = FALSE;
        make_canvas_item_one(group, item);
      }
    }
  }
}

gboolean do_hand_scrollto(gpointer data)
{
  ui.hand_scrollto_pending = FALSE;
  gnome_canvas_scroll_to(canvas, ui.hand_scrollto_cx, ui.hand_scrollto_cy);
  return FALSE;
}

void do_hand(GdkEvent *event)
{
  double pt[2];
  int cx, cy;
  
  get_pointer_coords(event, pt);
  pt[0] += ui.cur_page->hoffset;
  pt[1] += ui.cur_page->voffset;
  gnome_canvas_get_scroll_offsets(canvas, &cx, &cy);
  ui.hand_scrollto_cx = cx - (pt[0]-ui.hand_refpt[0])*ui.zoom;
  ui.hand_scrollto_cy = cy - (pt[1]-ui.hand_refpt[1])*ui.zoom;
  if (!ui.hand_scrollto_pending) g_idle_add(do_hand_scrollto, NULL);
  ui.hand_scrollto_pending = TRUE;
}

/************ TEXT FUNCTIONS **************/

// to make it easier to copy/paste at end of text box
#define WIDGET_RIGHT_MARGIN 10

void resize_textview(gpointer *toplevel, gpointer *data)
{
  GtkTextView *w;
  int width, height;
  
  /* when the text changes, resize the GtkTextView accordingly */
  if (ui.cur_item_type!=ITEM_TEXT) return;
  w = GTK_TEXT_VIEW(ui.cur_item->widget);
  width = w->width + WIDGET_RIGHT_MARGIN;
  height = w->height;
  gnome_canvas_item_set(ui.cur_item->canvas_item, 
    "size-pixels", TRUE, 
    "width", (gdouble)width, "height", (gdouble)height, NULL);
  ui.cur_item->bbox.right = ui.cur_item->bbox.left + width/ui.zoom;
  ui.cur_item->bbox.bottom = ui.cur_item->bbox.top + height/ui.zoom;
}

void start_text(GdkEvent *event, struct Item *item)
{
  double pt[2];
  GtkTextBuffer *buffer;
  GnomeCanvasItem *canvas_item;
  PangoFontDescription *font_desc;
  GdkColor color;

  get_pointer_coords(event, pt);

  ui.cur_item_type = ITEM_TEXT;

  if (item==NULL) {
    item = g_new(struct Item, 1);
    item->text = NULL;
    item->canvas_item = NULL;
    item->bbox.left = pt[0];
    item->bbox.top = pt[1];
    item->bbox.right = ui.cur_page->width;
    item->bbox.bottom = pt[1]+100.;
    item->font_name = g_strdup(ui.font_name);
    item->font_size = ui.font_size;
    g_memmove(&(item->brush), ui.cur_brush, sizeof(struct Brush));
    ui.cur_layer->items = g_list_append(ui.cur_layer->items, item);
    ui.cur_layer->nitems++;
  }
  
  item->type = ITEM_TEMP_TEXT;
  ui.cur_item = item;
  
  font_desc = pango_font_description_from_string(item->font_name);
  pango_font_description_set_absolute_size(font_desc, 
      item->font_size*ui.zoom*PANGO_SCALE);
  item->widget = gtk_text_view_new();
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(item->widget));
  if (item->text!=NULL)
    gtk_text_buffer_set_text(buffer, item->text, -1);
  gtk_widget_modify_font(item->widget, font_desc);
  rgb_to_gdkcolor(item->brush.color_rgba, &color);
  gtk_widget_modify_text(item->widget, GTK_STATE_NORMAL, &color);
  pango_font_description_free(font_desc);

  canvas_item = gnome_canvas_item_new(ui.cur_layer->group,
    gnome_canvas_widget_get_type(),
    "x", item->bbox.left, "y", item->bbox.top, 
    "width", item->bbox.right-item->bbox.left, 
    "height", item->bbox.bottom-item->bbox.top,
    "widget", item->widget, NULL);
  // TODO: width/height?
  if (item->canvas_item!=NULL) {
    lower_canvas_item_to(ui.cur_layer->group, canvas_item, item->canvas_item);
    gtk_object_destroy(GTK_OBJECT(item->canvas_item));
  }
  item->canvas_item = canvas_item;

  gtk_widget_show(item->widget);
  ui.resize_signal_handler = 
    g_signal_connect((gpointer) winMain, "check_resize",
       G_CALLBACK(resize_textview), NULL);
  update_font_button();
  gtk_widget_set_sensitive(GET_COMPONENT("editPaste"), FALSE);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonPaste"), FALSE);
  gtk_widget_grab_focus(item->widget); 
}

void end_text(void)
{
  GtkTextBuffer *buffer;
  GtkTextIter start, end;
  gchar *new_text;
  struct UndoErasureData *erasure;
  GnomeCanvasItem *tmpitem;

  if (ui.cur_item_type!=ITEM_TEXT) return; // nothing for us to do!

  // finalize the text that's been edited... 
  buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui.cur_item->widget));
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  ui.cur_item->type = ITEM_TEXT;
  new_text = gtk_text_buffer_get_text(buffer, &start, &end, TRUE);
  ui.cur_item_type = ITEM_NONE;
  gtk_widget_set_sensitive(GET_COMPONENT("editPaste"), TRUE);
  gtk_widget_set_sensitive(GET_COMPONENT("buttonPaste"), TRUE);
  
  if (strlen(new_text)==0) { // erase object and cancel
    g_free(new_text);
    g_signal_handler_disconnect(winMain, ui.resize_signal_handler);
    gtk_object_destroy(GTK_OBJECT(ui.cur_item->canvas_item));
    ui.cur_item->canvas_item = NULL;
    if (ui.cur_item->text == NULL) // nothing happened
      g_free(ui.cur_item->font_name);
    else { // treat this as an erasure
      prepare_new_undo();
      undo->type = ITEM_ERASURE;
      undo->layer = ui.cur_layer;
      erasure = (struct UndoErasureData *)g_malloc(sizeof(struct UndoErasureData));
      erasure->item = ui.cur_item;
      erasure->npos = g_list_index(ui.cur_layer->items, ui.cur_item);
      erasure->nrepl = 0;
      erasure->replacement_items = NULL;
      undo->erasurelist = g_list_append(NULL, erasure);
    }
    ui.cur_layer->items = g_list_remove(ui.cur_layer->items, ui.cur_item);
    ui.cur_layer->nitems--;
    ui.cur_item = NULL;
    return;
  }

  // store undo data
  if (ui.cur_item->text==NULL || strcmp(ui.cur_item->text, new_text)) {
    prepare_new_undo();
    if (ui.cur_item->text == NULL) undo->type = ITEM_TEXT; 
    else undo->type = ITEM_TEXT_EDIT;
    undo->layer = ui.cur_layer;
    undo->item = ui.cur_item;
    undo->str = ui.cur_item->text;
  }
  else g_free(ui.cur_item->text);

  ui.cur_item->text = new_text;
  ui.cur_item->widget = NULL;
  // replace the canvas item
  tmpitem = ui.cur_item->canvas_item;
  make_canvas_item_one(ui.cur_layer->group, ui.cur_item);
  update_item_bbox(ui.cur_item);
  lower_canvas_item_to(ui.cur_layer->group, ui.cur_item->canvas_item, tmpitem);
  gtk_object_destroy(GTK_OBJECT(tmpitem));
}

/* update the items in the canvas so they're of the right font size */

void update_text_item_displayfont(struct Item *item)
{
  PangoFontDescription *font_desc;

  if (item->type != ITEM_TEXT && item->type != ITEM_TEMP_TEXT) return;
  if (item->canvas_item==NULL) return;
  font_desc = pango_font_description_from_string(item->font_name);
  pango_font_description_set_absolute_size(font_desc, 
        item->font_size*ui.zoom*PANGO_SCALE);
  if (item->type == ITEM_TEMP_TEXT)
    gtk_widget_modify_font(item->widget, font_desc);
  else {
    gnome_canvas_item_set(item->canvas_item, "font-desc", font_desc, NULL);
    update_item_bbox(item);
  }
  pango_font_description_free(font_desc);
}

void rescale_text_items(void)
{
  GList *pagelist, *layerlist, *itemlist;
  
  for (pagelist = journal.pages; pagelist!=NULL; pagelist = pagelist->next)
    for (layerlist = ((struct Page *)pagelist->data)->layers; layerlist!=NULL; layerlist = layerlist->next)
      for (itemlist = ((struct Layer *)layerlist->data)->items; itemlist!=NULL; itemlist = itemlist->next)
        update_text_item_displayfont((struct Item *)itemlist->data);
}

struct Item *click_is_in_object(struct Layer *layer, double x, double y)
{
  GList *itemlist;
  struct Item *item, *object_item;
  object_item = NULL;
  
  for (itemlist = ui.cur_layer->items; itemlist!=NULL; itemlist = itemlist->next) {
    item = (struct Item *)itemlist->data;
    if (item_under_point(item, x, y) && (item->type == ITEM_TEXT || item->type == ITEM_IMAGE)) 
      object_item = item;
  }
  return object_item;
}

struct Item *click_is_in_text(struct Layer *layer, double x, double y)
{
  struct Item *item = click_is_in_object(layer, x, y);
  return (item && item->type == ITEM_TEXT ? item : NULL);
}

void refont_text_item(struct Item *item, gchar *font_name, double font_size)
{
  if (!strcmp(font_name, item->font_name) && font_size==item->font_size) return;
  if (item->text!=NULL) {
    prepare_new_undo();
    undo->type = ITEM_TEXT_ATTRIB;
    undo->item = item;
    undo->str = item->font_name;
    undo->val_x = item->font_size;
    undo->brush = (struct Brush *)g_memdup(&(item->brush), sizeof(struct Brush));
  }
  else g_free(item->font_name);
  item->font_name = g_strdup(font_name);
  if (font_size>0.) item->font_size = font_size;
  update_text_item_displayfont(item);
}

void process_font_sel(gchar *str)
{
  gchar *p, *q;
  struct Item *it;
  gdouble size;
  GList *list;
  gboolean undo_cont;

  p = strrchr(str, ' ');
  if (p!=NULL) { 
    size = g_strtod(p+1, &q);
    if (*q!=0 || size<1.) size=0.;
    else *p=0;
  }
  else size=0.;
  g_free(ui.font_name);
  ui.font_name = str;  
  if (size>0.) ui.font_size = size;
  undo_cont = FALSE;   
  // if there's a current text item, re-font it
  if (ui.cur_item_type == ITEM_TEXT) {
    refont_text_item(ui.cur_item, str, size);
    undo_cont = (ui.cur_item->text!=NULL);   
  }
  // if there's a current selection, re-font it
  if (ui.selection!=NULL) 
    for (list=ui.selection->items; list!=NULL; list=list->next) {
      it = (struct Item *)list->data;
      if (it->type == ITEM_TEXT) {   
        if (undo_cont) undo->multiop |= MULTIOP_CONT_REDO;
        refont_text_item(it, str, size);
        if (undo_cont) undo->multiop |= MULTIOP_CONT_UNDO;
        undo_cont = TRUE;
      }
    }  
  update_font_button();
}

/************ IMAGE FUNCTIONS **************/

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

  if (ui.default_path!=NULL) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (dialog), ui.default_path);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
    gtk_widget_destroy(dialog);
    return;
  }
  filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
  gtk_widget_destroy(dialog);
  
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
    item->image_path = filename;
	  printf("insert_image: '%s' image_path: '%s'\n",filename,item->image_path);
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
