#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <math.h>
#include <gtk/gtk.h>
#include <libart_lgpl/art_vpath_bpath.h>
#include <libart_lgpl/art_svp_vpath.h>
#include <libart_lgpl/art_rect_svp.h>
#include "xournal.h"
#include "xo-callbacks.h"
#include "xo-interface.h"
#include "xo-support.h"
#include "xo-misc.h"
#include "xo-paint.h"
#include "xo-selection.h"
#include "xo-clipboard.h"

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

void get_possible_resize_direction(double *pt, gboolean *l, gboolean *r, gboolean *t, gboolean *b)
{
  double h_corner_margin, v_corner_margin, hmargin, vmargin, resize_margin;
  if (ui.selection==NULL) return;
  if (ui.cur_layer != ui.selection->layer) return;

  resize_margin = RESIZE_MARGIN/ui.zoom;
  hmargin = (ui.selection->bbox.right-ui.selection->bbox.left)*0.3;
  if (hmargin>resize_margin) hmargin = resize_margin;
  vmargin = (ui.selection->bbox.bottom-ui.selection->bbox.top)*0.3;
  if (vmargin>resize_margin) vmargin = resize_margin;

  // make sure the click is within a box slightly bigger than the selection rectangle
  if (pt[0]<ui.selection->bbox.left-resize_margin || 
      pt[0]>ui.selection->bbox.right+resize_margin ||
      pt[1]<ui.selection->bbox.top-resize_margin || 
      pt[1]>ui.selection->bbox.bottom+resize_margin) {
    *l = *r = *t = *b = FALSE;
    return;
  }

  // If we got here, and if the click is near the edge, it's a resize operation.
  // keep track of which edges we're close to, since those are the ones which should move

  h_corner_margin = 0.25 * fabs(ui.selection->bbox.right - ui.selection->bbox.left);
  v_corner_margin = 0.25 * fabs(ui.selection->bbox.top - ui.selection->bbox.bottom);

  *l = (pt[0]<ui.selection->bbox.left+hmargin);
  *r = (pt[0]>ui.selection->bbox.right-hmargin);
  if (*l || *r) 
    vmargin = v_corner_margin;
  *t = (pt[1]<ui.selection->bbox.top+vmargin);
  *b = (pt[1]>ui.selection->bbox.bottom-vmargin);
  if (*t || *b) { // redo left/right tests with new margin
    hmargin = h_corner_margin;
    *l = (pt[0]<ui.selection->bbox.left+hmargin);
    *r = (pt[0]>ui.selection->bbox.right-hmargin);
  }
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


void reset_selection(void)
{
  if (ui.selection == NULL) return;
  if (ui.selection->canvas_item != NULL) 
    gtk_object_destroy(GTK_OBJECT(ui.selection->canvas_item));

  if( ui.selection->closedlassopath  != NULL )
    gnome_canvas_path_def_unref(ui.selection->closedlassopath);  
  if( ui.selection->lassopath  != NULL )
    gnome_canvas_path_def_unref(ui.selection->lassopath);  
  if(ui.selection->lasso != NULL ) 
    gtk_object_destroy(GTK_OBJECT(ui.selection->lasso)); 

  // if(ui.selection->lassoclip != NULL ) 
  //  gtk_object_destroy(GTK_OBJECT(ui.selection->lassoclip)); 

 
  g_list_free(ui.selection->items);
  g_free(ui.selection);
  ui.selection = NULL;
  update_copy_paste_enabled();
  update_color_menu();
  update_thickness_buttons();
  update_color_buttons();
  update_font_button();
  update_cursor();
}
