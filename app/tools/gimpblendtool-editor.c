/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <gegl.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpmath/gimpmath.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "tools-types.h"

#include "operations/gimp-operation-config.h"

#include "core/gimpdata.h"
#include "core/gimpgradient.h"
#include "core/gimp-gradients.h"
#include "core/gimpimage.h"

#include "widgets/gimpcolorpanel.h"
#include "widgets/gimpeditor.h"
#include "widgets/gimpwidgets-utils.h"

#include "display/gimpdisplay.h"
#include "display/gimpdisplayshell.h"
#include "display/gimptoolgui.h"
#include "display/gimptoolline.h"

#include "gimpblendoptions.h"
#include "gimpblendtool.h"
#include "gimpblendtool-editor.h"

#include "gimp-intl.h"


#define EPSILON 1e-10


typedef enum
{
  DIRECTION_NONE,
  DIRECTION_LEFT,
  DIRECTION_RIGHT
} Direction;


/*  local function prototypes  */

static gboolean              gimp_blend_tool_editor_line_can_add_slider        (GimpToolLine         *line,
                                                                                gdouble               value,
                                                                                GimpBlendTool        *blend_tool);
static gint                  gimp_blend_tool_editor_line_add_slider            (GimpToolLine         *line,
                                                                                gdouble               value,
                                                                                GimpBlendTool        *blend_tool);
static void                  gimp_blend_tool_editor_line_remove_slider         (GimpToolLine         *line,
                                                                                gint                  slider,
                                                                                GimpBlendTool        *blend_tool);
static void                  gimp_blend_tool_editor_line_selection_changed     (GimpToolLine         *line,
                                                                                GimpBlendTool        *blend_tool);
static gboolean              gimp_blend_tool_editor_line_handle_clicked        (GimpToolLine         *line,
                                                                                gint                  handle,
                                                                                GdkModifierType       state,
                                                                                GimpButtonPressType   press_type,
                                                                                GimpBlendTool        *blend_tool);

static void                  gimp_blend_tool_editor_gui_response               (GimpToolGui          *gui,
                                                                                gint                  response_id,
                                                                                GimpBlendTool        *blend_tool);

static void                  gimp_blend_tool_editor_color_entry_color_changed  (GimpColorButton       *button,
                                                                                GimpBlendTool         *blend_tool);

static void                  gimp_blend_tool_editor_color_entry_type_changed   (GtkComboBox           *combo,
                                                                                GimpBlendTool         *blend_tool);

static void                  gimp_blend_tool_editor_endpoint_se_value_changed  (GimpSizeEntry         *se,
                                                                                GimpBlendTool         *blend_tool);

static gboolean              gimp_blend_tool_editor_is_gradient_editable       (GimpBlendTool        *blend_tool);

static gboolean              gimp_blend_tool_editor_handle_is_endpoint         (GimpBlendTool        *blend_tool,
                                                                                gint                  handle);
static gboolean              gimp_blend_tool_editor_handle_is_stop             (GimpBlendTool        *blend_tool,
                                                                                gint                  handle);
static gboolean              gimp_blend_tool_editor_handle_is_midpoint         (GimpBlendTool        *blend_tool,
                                                                                gint                  handle);
static GimpGradientSegment * gimp_blend_tool_editor_handle_get_segment         (GimpBlendTool        *blend_tool,
                                                                                gint                  handle);

static void                  gimp_blend_tool_editor_block_handlers             (GimpBlendTool        *blend_tool);
static void                  gimp_blend_tool_editor_unblock_handlers           (GimpBlendTool        *blend_tool);
static gboolean              gimp_blend_tool_editor_are_handlers_blocked       (GimpBlendTool        *blend_tool);

static void                  gimp_blend_tool_editor_freeze_gradient            (GimpBlendTool        *blend_tool);
static void                  gimp_blend_tool_editor_thaw_gradient              (GimpBlendTool        *blend_tool);

static gint                  gimp_blend_tool_editor_add_stop                   (GimpBlendTool        *blend_tool,
                                                                                gdouble               value);

static void                  gimp_blend_tool_editor_update_sliders             (GimpBlendTool        *blend_tool);

static GtkWidget           * gimp_blend_tool_editor_color_entry_new            (GimpBlendTool        *blend_tool,
                                                                                const gchar          *title,
                                                                                Direction             direction,
                                                                                GtkWidget            *chain_button,
                                                                                GtkWidget           **color_panel,
                                                                                GtkWidget           **type_combo);
static void                  gimp_blend_tool_editor_init_endpoint_gui          (GimpBlendTool        *blend_tool);
static void                  gimp_blend_tool_editor_update_endpoint_gui        (GimpBlendTool        *blend_tool,
                                                                                gint                  selection);
static void                  gimp_blend_tool_editor_update_gui                 (GimpBlendTool        *blend_tool);


/*  private functions  */


static gboolean
gimp_blend_tool_editor_line_can_add_slider (GimpToolLine  *line,
                                            gdouble        value,
                                            GimpBlendTool *blend_tool)
{
  GimpBlendOptions *options = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  gdouble           offset  = options->offset / 100.0;

  return gimp_blend_tool_editor_is_gradient_editable (blend_tool) &&
         value >= offset;
}

static gint
gimp_blend_tool_editor_line_add_slider (GimpToolLine  *line,
                                        gdouble        value,
                                        GimpBlendTool *blend_tool)
{
  GimpBlendOptions *options       = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpPaintOptions *paint_options = GIMP_PAINT_OPTIONS (options);
  gdouble           offset        = options->offset / 100.0;

  /* adjust slider value according to the offset */
  value = (value - offset) / (1.0 - offset);

  /* flip the slider value, if necessary */
  if (paint_options->gradient_options->gradient_reverse)
    value = 1.0 - value;

  return gimp_blend_tool_editor_add_stop (blend_tool, value);
}

static void
gimp_blend_tool_editor_line_remove_slider (GimpToolLine  *line,
                                           gint           slider,
                                           GimpBlendTool *blend_tool)
{
  GimpGradientSegment *seg;

  gimp_blend_tool_editor_freeze_gradient (blend_tool);

  seg = gimp_blend_tool_editor_handle_get_segment (blend_tool, slider);

  gimp_gradient_segment_range_merge (blend_tool->gradient,
                                     seg, seg->next, NULL, NULL);

  gimp_blend_tool_editor_thaw_gradient (blend_tool);
}

static void
gimp_blend_tool_editor_line_selection_changed (GimpToolLine  *line,
                                               GimpBlendTool *blend_tool)
{
  if (blend_tool->gui)
    {
      /* hide all color dialogs */
      gimp_color_panel_dialog_response (
        GIMP_COLOR_PANEL (blend_tool->endpoint_color_panel),
        GIMP_COLOR_DIALOG_OK);
    }

  gimp_blend_tool_editor_update_gui (blend_tool);
}

static gboolean
gimp_blend_tool_editor_line_handle_clicked (GimpToolLine        *line,
                                            gint                 handle,
                                            GdkModifierType      state,
                                            GimpButtonPressType  press_type,
                                            GimpBlendTool       *blend_tool)
{
  if (gimp_blend_tool_editor_handle_is_midpoint (blend_tool, handle))
    {
      if (press_type == GIMP_BUTTON_PRESS_DOUBLE &&
          gimp_blend_tool_editor_is_gradient_editable (blend_tool))
        {
          const GimpControllerSlider *sliders;
          gint                        stop;

          sliders = gimp_tool_line_get_sliders (line, NULL);

          if (sliders[handle].value > sliders[handle].min + EPSILON &&
              sliders[handle].value < sliders[handle].max - EPSILON)
            {
              stop = gimp_blend_tool_editor_add_stop (blend_tool,
                                                      sliders[handle].value);

              gimp_tool_line_set_selection (line, stop);
            }

          /* return FALSE, so that the new slider can be dragged immediately */
          return FALSE;
        }
    }

  return FALSE;
}


static void
gimp_blend_tool_editor_gui_response (GimpToolGui   *gui,
                                     gint           response_id,
                                     GimpBlendTool *blend_tool)
{
  switch (response_id)
    {
    default:
      gimp_tool_line_set_selection (GIMP_TOOL_LINE (blend_tool->widget),
                                    GIMP_TOOL_LINE_HANDLE_NONE);
      break;
    }
}

static void
gimp_blend_tool_editor_color_entry_color_changed (GimpColorButton *button,
                                                  GimpBlendTool   *blend_tool)
{
  GimpBlendOptions    *options       = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpPaintOptions    *paint_options = GIMP_PAINT_OPTIONS (options);
  gint                 selection;
  GimpRGB              color;
  Direction            direction;
  GtkWidget           *chain_button;
  GimpGradientSegment *seg;

  if (gimp_blend_tool_editor_are_handlers_blocked (blend_tool))
    return;

  selection =
    gimp_tool_line_get_selection (GIMP_TOOL_LINE (blend_tool->widget));

  gimp_color_button_get_color (button, &color);

  direction =
    GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
                                        "gimp-blend-tool-editor-direction"));
  chain_button = g_object_get_data (G_OBJECT (button),
                                    "gimp-blend-tool-editor-chain-button");

  gimp_blend_tool_editor_freeze_gradient (blend_tool);

  /* swap the endpoint handles, if necessary */
  if (paint_options->gradient_options->gradient_reverse)
    {
      switch (selection)
        {
        case GIMP_TOOL_LINE_HANDLE_START:
          selection = GIMP_TOOL_LINE_HANDLE_END;
          break;

        case GIMP_TOOL_LINE_HANDLE_END:
          selection = GIMP_TOOL_LINE_HANDLE_START;
          break;
        }
    }

  seg = gimp_blend_tool_editor_handle_get_segment (blend_tool, selection);

  switch (selection)
    {
    case GIMP_TOOL_LINE_HANDLE_START:
      seg->left_color      = color;
      seg->left_color_type = GIMP_GRADIENT_COLOR_FIXED;
      break;

    case GIMP_TOOL_LINE_HANDLE_END:
      seg->right_color      = color;
      seg->right_color_type = GIMP_GRADIENT_COLOR_FIXED;
      break;

    default:
      if (direction == DIRECTION_LEFT ||
          (chain_button               &&
           gimp_chain_button_get_active (GIMP_CHAIN_BUTTON (chain_button))))
        {
          seg->right_color      = color;
          seg->right_color_type = GIMP_GRADIENT_COLOR_FIXED;
        }

      if (direction == DIRECTION_RIGHT ||
          (chain_button                &&
           gimp_chain_button_get_active (GIMP_CHAIN_BUTTON (chain_button))))
        {
          seg->next->left_color      = color;
          seg->next->left_color_type = GIMP_GRADIENT_COLOR_FIXED;
        }
    }

  gimp_blend_tool_editor_thaw_gradient (blend_tool);
}

static void
gimp_blend_tool_editor_color_entry_type_changed (GtkComboBox   *combo,
                                                 GimpBlendTool *blend_tool)
{
  GimpBlendOptions    *options       = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpPaintOptions    *paint_options = GIMP_PAINT_OPTIONS (options);
  gint                 selection;
  gint                 color_type;
  Direction            direction;
  GtkWidget           *chain_button;
  GimpGradientSegment *seg;

  if (gimp_blend_tool_editor_are_handlers_blocked (blend_tool))
    return;

  selection =
    gimp_tool_line_get_selection (GIMP_TOOL_LINE (blend_tool->widget));

  if (! gimp_int_combo_box_get_active (GIMP_INT_COMBO_BOX (combo), &color_type))
    return;

  direction =
    GPOINTER_TO_INT (g_object_get_data (G_OBJECT (combo),
                                        "gimp-blend-tool-editor-direction"));
  chain_button = g_object_get_data (G_OBJECT (combo),
                                    "gimp-blend-tool-editor-chain-button");

  gimp_blend_tool_editor_freeze_gradient (blend_tool);

  /* swap the endpoint handles, if necessary */
  if (paint_options->gradient_options->gradient_reverse)
    {
      switch (selection)
        {
        case GIMP_TOOL_LINE_HANDLE_START:
          selection = GIMP_TOOL_LINE_HANDLE_END;
          break;

        case GIMP_TOOL_LINE_HANDLE_END:
          selection = GIMP_TOOL_LINE_HANDLE_START;
          break;
        }
    }

  seg = gimp_blend_tool_editor_handle_get_segment (blend_tool, selection);

  switch (selection)
    {
    case GIMP_TOOL_LINE_HANDLE_START:
      seg->left_color_type = color_type;
      break;

    case GIMP_TOOL_LINE_HANDLE_END:
      seg->right_color_type = color_type;
      break;

    default:
      if (direction == DIRECTION_LEFT ||
          (chain_button               &&
           gimp_chain_button_get_active (GIMP_CHAIN_BUTTON (chain_button))))
        {
          seg->right_color_type = color_type;
        }

      if (direction == DIRECTION_RIGHT ||
          (chain_button                &&
           gimp_chain_button_get_active (GIMP_CHAIN_BUTTON (chain_button))))
        {
          seg->next->left_color_type = color_type;
        }
    }

  gimp_blend_tool_editor_thaw_gradient (blend_tool);
}

static void
gimp_blend_tool_editor_endpoint_se_value_changed (GimpSizeEntry *se,
                                                  GimpBlendTool *blend_tool)
{
  gint    selection;
  gdouble x;
  gdouble y;

  if (gimp_blend_tool_editor_are_handlers_blocked (blend_tool))
    return;

  selection =
    gimp_tool_line_get_selection (GIMP_TOOL_LINE (blend_tool->widget));

  x = gimp_size_entry_get_refval (se, 0);
  y = gimp_size_entry_get_refval (se, 1);

  gimp_blend_tool_editor_block_handlers (blend_tool);

  switch (selection)
    {
    case GIMP_TOOL_LINE_HANDLE_START:
      g_object_set (blend_tool->widget,
                    "x1", x,
                    "y1", y,
                    NULL);
      break;

    case GIMP_TOOL_LINE_HANDLE_END:
      g_object_set (blend_tool->widget,
                    "x2", x,
                    "y2", y,
                    NULL);
      break;

    default:
      g_assert_not_reached ();
    }

  gimp_blend_tool_editor_unblock_handlers (blend_tool);
}

static gboolean
gimp_blend_tool_editor_is_gradient_editable (GimpBlendTool *blend_tool)
{
  GimpBlendOptions *options = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);

  return ! options->modify_active ||
         gimp_data_is_writable (GIMP_DATA (blend_tool->gradient));
}

static gboolean
gimp_blend_tool_editor_handle_is_endpoint (GimpBlendTool *blend_tool,
                                           gint           handle)
{
  return handle == GIMP_TOOL_LINE_HANDLE_START ||
         handle == GIMP_TOOL_LINE_HANDLE_END;
}

static gboolean
gimp_blend_tool_editor_handle_is_stop (GimpBlendTool *blend_tool,
                                       gint           handle)
{
  gint n_sliders;

  gimp_tool_line_get_sliders (GIMP_TOOL_LINE (blend_tool->widget), &n_sliders);

  return handle >= 0 && handle < n_sliders / 2;
}

static gboolean
gimp_blend_tool_editor_handle_is_midpoint (GimpBlendTool *blend_tool,
                                           gint           handle)
{
  gint n_sliders;

  gimp_tool_line_get_sliders (GIMP_TOOL_LINE (blend_tool->widget), &n_sliders);

  return handle >= n_sliders / 2;
}

static GimpGradientSegment *
gimp_blend_tool_editor_handle_get_segment (GimpBlendTool *blend_tool,
                                           gint           handle)
{
  switch (handle)
    {
    case GIMP_TOOL_LINE_HANDLE_START:
      return blend_tool->gradient->segments;

    case GIMP_TOOL_LINE_HANDLE_END:
      return gimp_gradient_segment_get_last (blend_tool->gradient->segments);

    default:
      {
        const GimpControllerSlider *sliders;
        gint                        n_sliders;
        gint                        seg_i;

        sliders = gimp_tool_line_get_sliders (GIMP_TOOL_LINE (blend_tool->widget),
                                              &n_sliders);

        g_assert (handle >= 0 && handle < n_sliders);

        seg_i = GPOINTER_TO_INT (sliders[handle].data);

        return gimp_gradient_segment_get_nth (blend_tool->gradient->segments,
                                              seg_i);
      }
    }
}

static void
gimp_blend_tool_editor_block_handlers (GimpBlendTool *blend_tool)
{
  blend_tool->block_handlers_count++;
}

static void
gimp_blend_tool_editor_unblock_handlers (GimpBlendTool *blend_tool)
{
  g_assert (blend_tool->block_handlers_count > 0);

  blend_tool->block_handlers_count--;
}

static gboolean
gimp_blend_tool_editor_are_handlers_blocked (GimpBlendTool *blend_tool)
{
  return blend_tool->block_handlers_count > 0;
}

static void
gimp_blend_tool_editor_freeze_gradient (GimpBlendTool *blend_tool)
{
  GimpBlendOptions *options = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpGradient     *custom;

  gimp_blend_tool_editor_block_handlers (blend_tool);

  custom = gimp_gradients_get_custom (GIMP_CONTEXT (options)->gimp);

  if (blend_tool->gradient == custom || options->modify_active)
    {
      g_assert (gimp_blend_tool_editor_is_gradient_editable (blend_tool));

      gimp_data_freeze (GIMP_DATA (blend_tool->gradient));
    }
  else
    {
      /* copy the active gradient to the custom gradient, and make the custom
       * gradient active.
       */
      gimp_data_freeze (GIMP_DATA (custom));

      gimp_data_copy (GIMP_DATA (custom), GIMP_DATA (blend_tool->gradient));

      gimp_context_set_gradient (GIMP_CONTEXT (options), custom);

      g_assert (blend_tool->gradient == custom);
      g_assert (gimp_blend_tool_editor_is_gradient_editable (blend_tool));
    }
}

static void
gimp_blend_tool_editor_thaw_gradient(GimpBlendTool *blend_tool)
{
  gimp_data_thaw (GIMP_DATA (blend_tool->gradient));

  gimp_blend_tool_editor_update_sliders (blend_tool);
  gimp_blend_tool_editor_update_gui (blend_tool);

  gimp_blend_tool_editor_unblock_handlers (blend_tool);
}

static gint
gimp_blend_tool_editor_add_stop (GimpBlendTool *blend_tool,
                                 gdouble        value)
{
  GimpBlendOptions    *options = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpGradientSegment *seg;
  gint                 stop;

  gimp_blend_tool_editor_freeze_gradient (blend_tool);

  gimp_gradient_split_at (blend_tool->gradient,
                          GIMP_CONTEXT (options), NULL, value, &seg, NULL);

  stop =
    gimp_gradient_segment_range_get_n_segments (blend_tool->gradient,
                                                blend_tool->gradient->segments,
                                                seg) - 1;

  gimp_blend_tool_editor_thaw_gradient (blend_tool);

  return stop;
}

static void
gimp_blend_tool_editor_update_sliders (GimpBlendTool *blend_tool)
{
  GimpBlendOptions     *options       = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpPaintOptions     *paint_options = GIMP_PAINT_OPTIONS (options);
  gdouble               offset        = options->offset / 100.0;
  gboolean              editable;
  GimpControllerSlider *sliders;
  gint                  n_sliders;
  gint                  n_segments;
  GimpGradientSegment  *seg;
  GimpControllerSlider *slider;
  gint                  i;

  if (! blend_tool->widget || options->instant)
    return;

  editable = gimp_blend_tool_editor_is_gradient_editable (blend_tool);

  n_segments = gimp_gradient_segment_range_get_n_segments (
    blend_tool->gradient, blend_tool->gradient->segments, NULL);

  n_sliders = (n_segments - 1) + /* gradient stops, between each adjacent
                                  * pair of segments */
              (n_segments);      /* midpoints, inside each segment */

  sliders = g_new (GimpControllerSlider, n_sliders);

  slider = sliders;

  /* initialize the gradient-stop sliders */
  for (seg = blend_tool->gradient->segments, i = 0;
       seg->next;
       seg = seg->next, i++)
    {
      *slider = GIMP_CONTROLLER_SLIDER_DEFAULT;

      slider->value     = seg->right;
      slider->min       = seg->left;
      slider->max       = seg->next->right;

      slider->movable   = editable;
      slider->removable = editable;

      slider->data      = GINT_TO_POINTER (i);

      slider++;
    }

  /* initialize the midpoint sliders */
  for (seg = blend_tool->gradient->segments, i = 0;
       seg;
       seg = seg->next, i++)
    {
      *slider = GIMP_CONTROLLER_SLIDER_DEFAULT;

      slider->value    = seg->middle;
      slider->min      = seg->left;
      slider->max      = seg->right;

      /* hide midpoints of zero-length segments, since they'd otherwise
       * prevent the segment's endpoints from being selected
       */
      slider->visible  = fabs (slider->max - slider->min) > EPSILON;
      slider->movable  = editable;

      slider->autohide = TRUE;
      slider->type     = GIMP_HANDLE_FILLED_CIRCLE;
      slider->size     = 0.6;

      slider->data     = GINT_TO_POINTER (i);

      slider++;
    }

  /* flip the slider limits and values, if necessary */
  if (paint_options->gradient_options->gradient_reverse)
    {
      for (i = 0; i < n_sliders; i++)
        {
          gdouble temp;

          sliders[i].value = 1.0 - sliders[i].value;
          temp             = sliders[i].min;
          sliders[i].min   = 1.0 - sliders[i].max;
          sliders[i].max   = 1.0 - temp;
        }
    }

  /* adjust the sliders according to the offset */
  for (i = 0; i < n_sliders; i++)
    {
      sliders[i].value = (1.0 - offset) * sliders[i].value + offset;
      sliders[i].min   = (1.0 - offset) * sliders[i].min   + offset;
      sliders[i].max   = (1.0 - offset) * sliders[i].max   + offset;
    }

  /* avoid updating the gradient in gimp_blend_tool_editor_line_changed() */
  gimp_blend_tool_editor_block_handlers (blend_tool);

  gimp_tool_line_set_sliders (GIMP_TOOL_LINE (blend_tool->widget),
                              sliders, n_sliders);

  gimp_blend_tool_editor_unblock_handlers (blend_tool);

  g_free (sliders);
}

static GtkWidget *
gimp_blend_tool_editor_color_entry_new (GimpBlendTool  *blend_tool,
                                        const gchar    *title,
                                        Direction       direction,
                                        GtkWidget      *chain_button,
                                        GtkWidget     **color_panel,
                                        GtkWidget     **type_combo)
{
  GimpContext *context = GIMP_CONTEXT (GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool));
  GtkWidget   *hbox;
  GtkWidget   *button;
  GtkWidget   *combo;
  GimpRGB      color   = {};

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

  /* the color panel */
  *color_panel = button = gimp_color_panel_new (title, &color,
                                                GIMP_COLOR_AREA_SMALL_CHECKS,
                                                24, 24);
  gimp_color_button_set_update (GIMP_COLOR_BUTTON (button), TRUE);
  gimp_color_panel_set_context (GIMP_COLOR_PANEL (button), context);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  g_object_set_data (G_OBJECT (button),
                     "gimp-blend-tool-editor-direction",
                     GINT_TO_POINTER (direction));
  g_object_set_data (G_OBJECT (button),
                     "gimp-blend-tool-editor-chain-button",
                     chain_button);

  g_signal_connect (button, "color-changed",
                    G_CALLBACK (gimp_blend_tool_editor_color_entry_color_changed),
                    blend_tool);

  /* the color type combo */
  *type_combo = combo = gimp_enum_combo_box_new (GIMP_TYPE_GRADIENT_COLOR);
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, TRUE, 0);
  gtk_widget_show (combo);

  g_object_set_data (G_OBJECT (combo),
                     "gimp-blend-tool-editor-direction",
                     GINT_TO_POINTER (direction));
  g_object_set_data (G_OBJECT (combo),
                     "gimp-blend-tool-editor-chain-button",
                     chain_button);

  g_signal_connect (combo, "changed",
                    G_CALLBACK (gimp_blend_tool_editor_color_entry_type_changed),
                    blend_tool);

  return hbox;
}

static void
gimp_blend_tool_editor_init_endpoint_gui (GimpBlendTool *blend_tool)
{
  GimpDisplay      *display = GIMP_TOOL (blend_tool)->display;
  GimpDisplayShell *shell   = gimp_display_get_shell (display);
  GimpImage        *image   = gimp_display_get_image (display);
  gdouble           xres;
  gdouble           yres;
  GtkWidget        *editor;
  GtkWidget        *table;
  GtkWidget        *label;
  GtkWidget        *spinbutton;
  GtkWidget        *se;
  GtkWidget        *hbox;
  gint              row     = 0;

  gimp_image_get_resolution (image, &xres, &yres);

  /* the endpoint editor */
  blend_tool->endpoint_editor =
  editor                      = gimp_editor_new ();
  gtk_box_pack_start (GTK_BOX (gimp_tool_gui_get_vbox (blend_tool->gui)),
                      editor, FALSE, TRUE, 0);

  /* the main table */
  table = gtk_table_new (1, 2, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 4);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_box_pack_start (GTK_BOX (editor), table, FALSE, TRUE, 0);
  gtk_widget_show (table);

  /* the position labels */
  label = gtk_label_new (_("X:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                    GTK_SHRINK | GTK_FILL, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  label = gtk_label_new (_("Y:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, row + 1, row + 2,
                    GTK_SHRINK | GTK_FILL, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  /* the position size entry */
  spinbutton = gtk_spin_button_new_with_range (0.0, 0.0, 1.0);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinbutton), TRUE);
  gtk_entry_set_width_chars (GTK_ENTRY (spinbutton), 6);

  blend_tool->endpoint_se =
  se                      = gimp_size_entry_new (1, GIMP_UNIT_PIXEL, "%a",
                                                 TRUE, TRUE, FALSE, 6,
                                                 GIMP_SIZE_ENTRY_UPDATE_SIZE);
  gtk_table_set_row_spacings (GTK_TABLE (se), 4);
  gtk_table_set_col_spacings (GTK_TABLE (se), 2);

  gimp_size_entry_add_field (GIMP_SIZE_ENTRY (se),
                             GTK_SPIN_BUTTON (spinbutton), NULL);
  gtk_table_attach_defaults (GTK_TABLE (se), spinbutton, 1, 2, 0, 1);
  gtk_widget_show (spinbutton);

  gtk_table_attach (GTK_TABLE (table), se, 1, 2, row, row + 2,
                    GTK_SHRINK | GTK_FILL | GTK_EXPAND,
                    GTK_SHRINK | GTK_FILL,
                    0, 0);
  gtk_widget_show (se);

  gimp_size_entry_set_unit (GIMP_SIZE_ENTRY (se), shell->unit);

  gimp_size_entry_set_resolution (GIMP_SIZE_ENTRY (se), 0, xres, FALSE);
  gimp_size_entry_set_resolution (GIMP_SIZE_ENTRY (se), 1, yres, FALSE);

  gimp_size_entry_set_refval_boundaries (GIMP_SIZE_ENTRY (se), 0,
                                         -GIMP_MAX_IMAGE_SIZE,
                                         GIMP_MAX_IMAGE_SIZE);
  gimp_size_entry_set_refval_boundaries (GIMP_SIZE_ENTRY (se), 1,
                                         -GIMP_MAX_IMAGE_SIZE,
                                         GIMP_MAX_IMAGE_SIZE);

  gimp_size_entry_set_size (GIMP_SIZE_ENTRY (se), 0,
                            0, gimp_image_get_width (image));
  gimp_size_entry_set_size (GIMP_SIZE_ENTRY (se), 1,
                            0, gimp_image_get_height (image));

  g_signal_connect (se, "value-changed",
                    G_CALLBACK (gimp_blend_tool_editor_endpoint_se_value_changed),
                    blend_tool);

  row += 2;

  /* the color label */
  label = gtk_label_new (_("Color:"));
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                    GTK_SHRINK | GTK_FILL, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  /* the color entry */
  hbox = gimp_blend_tool_editor_color_entry_new (
    blend_tool, _("Change Endpoint Color"), DIRECTION_NONE, NULL,
    &blend_tool->endpoint_color_panel, &blend_tool->endpoint_type_combo);
  gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, row, row + 1,
                    GTK_SHRINK | GTK_FILL | GTK_EXPAND,
                    GTK_SHRINK | GTK_FILL,
                    0, 0);
  gtk_widget_show (hbox);

  row++;
}

static void
gimp_blend_tool_editor_update_endpoint_gui (GimpBlendTool *blend_tool,
                                            gint           selection)
{
  GimpBlendOptions    *options       = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpPaintOptions    *paint_options = GIMP_PAINT_OPTIONS (options);
  GimpContext         *context       = GIMP_CONTEXT (options);
  gboolean             editable;
  GimpGradientSegment *seg;
  const gchar         *title;
  gdouble              x;
  gdouble              y;
  GimpRGB              color;
  GimpGradientColor    color_type;

  editable = gimp_blend_tool_editor_is_gradient_editable (blend_tool);

  switch (selection)
    {
    case GIMP_TOOL_LINE_HANDLE_START:
      g_object_get (blend_tool->widget,
                    "x1", &x,
                    "y1", &y,
                    NULL);
      break;

    case GIMP_TOOL_LINE_HANDLE_END:
      g_object_get (blend_tool->widget,
                    "x2", &x,
                    "y2", &y,
                    NULL);
      break;

    default:
      g_assert_not_reached ();
    }

  /* swap the endpoint handles, if necessary */
  if (paint_options->gradient_options->gradient_reverse)
    {
      switch (selection)
        {
        case GIMP_TOOL_LINE_HANDLE_START:
          selection = GIMP_TOOL_LINE_HANDLE_END;
          break;

        case GIMP_TOOL_LINE_HANDLE_END:
          selection = GIMP_TOOL_LINE_HANDLE_START;
          break;
        }
    }

  seg = gimp_blend_tool_editor_handle_get_segment (blend_tool, selection);

  switch (selection)
    {
    case GIMP_TOOL_LINE_HANDLE_START:
      title = _("Start Endpoint");

      gimp_gradient_segment_get_left_flat_color (blend_tool->gradient, context,
                                                 seg, &color);
      color_type = seg->left_color_type;
      break;

    case GIMP_TOOL_LINE_HANDLE_END:
      title = _("End Endpoint");

      gimp_gradient_segment_get_right_flat_color (blend_tool->gradient, context,
                                                  seg, &color);
      color_type = seg->right_color_type;
      break;

    default:
      g_assert_not_reached ();
    }

  gimp_tool_gui_set_title (blend_tool->gui, title);

  gimp_size_entry_set_refval (GIMP_SIZE_ENTRY (blend_tool->endpoint_se), 0, x);
  gimp_size_entry_set_refval (GIMP_SIZE_ENTRY (blend_tool->endpoint_se), 1, y);

  gimp_color_button_set_color (
    GIMP_COLOR_BUTTON (blend_tool->endpoint_color_panel), &color);
  gimp_int_combo_box_set_active (
    GIMP_INT_COMBO_BOX (blend_tool->endpoint_type_combo), color_type);

  gtk_widget_set_sensitive (blend_tool->endpoint_color_panel, editable);
  gtk_widget_set_sensitive (blend_tool->endpoint_type_combo,  editable);

  gtk_widget_show (blend_tool->endpoint_editor);
}

static void
gimp_blend_tool_editor_update_gui (GimpBlendTool *blend_tool)
{
  GimpBlendOptions *options = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);

  if (blend_tool->gradient && blend_tool->widget && ! options->instant)
    {
      gint selection;

      selection =
        gimp_tool_line_get_selection (GIMP_TOOL_LINE (blend_tool->widget));

      if (selection != GIMP_TOOL_LINE_HANDLE_NONE)
        {
          if (! blend_tool->gui)
            {
              GimpDisplayShell *shell;

              shell = gimp_tool_widget_get_shell (blend_tool->widget);

              blend_tool->gui =
                gimp_tool_gui_new (GIMP_TOOL (blend_tool)->tool_info,
                                   NULL, NULL, NULL, NULL,
                                   gtk_widget_get_screen (GTK_WIDGET (shell)),
                                   gimp_widget_get_monitor (GTK_WIDGET (shell)),
                                   TRUE,

                                   _("_Close"), GTK_RESPONSE_CLOSE,

                                   NULL);

              gimp_tool_gui_set_shell (blend_tool->gui, shell);
              gimp_tool_gui_set_viewable (blend_tool->gui,
                                          GIMP_VIEWABLE (blend_tool->gradient));
              gimp_tool_gui_set_auto_overlay (blend_tool->gui, TRUE);

              g_signal_connect (blend_tool->gui, "response",
                                G_CALLBACK (gimp_blend_tool_editor_gui_response),
                                blend_tool);

              gimp_blend_tool_editor_init_endpoint_gui (blend_tool);
            }

          gimp_blend_tool_editor_block_handlers (blend_tool);

          if (gimp_blend_tool_editor_handle_is_endpoint (blend_tool, selection))
            gimp_blend_tool_editor_update_endpoint_gui (blend_tool, selection);
          else
            gtk_widget_hide (blend_tool->endpoint_editor);

          gimp_blend_tool_editor_unblock_handlers (blend_tool);

          gimp_tool_gui_show (blend_tool->gui);

          return;
        }
    }

  if (blend_tool->gui)
    gimp_tool_gui_hide (blend_tool->gui);
}


/*  public functions  */


void
gimp_blend_tool_editor_options_notify (GimpBlendTool    *blend_tool,
                                       GimpToolOptions  *options,
                                       const GParamSpec *pspec)
{
  if (! strcmp (pspec->name, "modify-active"))
    {
      gimp_blend_tool_editor_update_sliders (blend_tool);
      gimp_blend_tool_editor_update_gui (blend_tool);
    }
  else if (! strcmp (pspec->name, "gradient-reverse"))
    {
      gimp_blend_tool_editor_update_sliders (blend_tool);

      /* if an endpoint is selected, swap the selected endpoint */
      if (blend_tool->widget)
        {
          gint selection;

          selection =
            gimp_tool_line_get_selection (GIMP_TOOL_LINE (blend_tool->widget));

          switch (selection)
            {
            case GIMP_TOOL_LINE_HANDLE_START:
              gimp_tool_line_set_selection (GIMP_TOOL_LINE (blend_tool->widget),
                                            GIMP_TOOL_LINE_HANDLE_END);
              break;

            case GIMP_TOOL_LINE_HANDLE_END:
              gimp_tool_line_set_selection (GIMP_TOOL_LINE (blend_tool->widget),
                                            GIMP_TOOL_LINE_HANDLE_START);
              break;
            }
        }
    }
  else if (blend_tool->render_node &&
           gegl_node_find_property (blend_tool->render_node, pspec->name))
    {
      gimp_blend_tool_editor_update_sliders (blend_tool);
    }
}

void
gimp_blend_tool_editor_start (GimpBlendTool *blend_tool)
{
  g_signal_connect (blend_tool->widget, "can-add-slider",
                    G_CALLBACK (gimp_blend_tool_editor_line_can_add_slider),
                    blend_tool);
  g_signal_connect (blend_tool->widget, "add-slider",
                    G_CALLBACK (gimp_blend_tool_editor_line_add_slider),
                    blend_tool);
  g_signal_connect (blend_tool->widget, "remove-slider",
                    G_CALLBACK (gimp_blend_tool_editor_line_remove_slider),
                    blend_tool);
  g_signal_connect (blend_tool->widget, "selection-changed",
                    G_CALLBACK (gimp_blend_tool_editor_line_selection_changed),
                    blend_tool);
  g_signal_connect (blend_tool->widget, "handle-clicked",
                    G_CALLBACK (gimp_blend_tool_editor_line_handle_clicked),
                    blend_tool);
}

void
gimp_blend_tool_editor_halt (GimpBlendTool *blend_tool)
{
  g_clear_object (&blend_tool->gui);
}

void
gimp_blend_tool_editor_line_changed (GimpBlendTool *blend_tool)
{
  GimpBlendOptions           *options       = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpPaintOptions           *paint_options = GIMP_PAINT_OPTIONS (options);
  gdouble                     offset        = options->offset / 100.0;
  const GimpControllerSlider *sliders;
  gint                        n_sliders;
  gint                        i;
  GimpGradientSegment        *seg;
  gboolean                    changed       = FALSE;

  if (gimp_blend_tool_editor_are_handlers_blocked (blend_tool))
    return;

  if (! blend_tool->gradient || offset == 1.0)
    return;

  sliders = gimp_tool_line_get_sliders (GIMP_TOOL_LINE (blend_tool->widget),
                                        &n_sliders);

  if (n_sliders == 0)
    return;

  /* update the midpoints first, since moving the gradient stops may change the
   * gradient's midpoints w.r.t. the sliders, but not the other way around.
   */
  for (seg = blend_tool->gradient->segments, i = n_sliders / 2;
       seg;
       seg = seg->next, i++)
    {
      gdouble value;

      value = sliders[i].value;

      /* adjust slider value according to the offset */
      value = (value - offset) / (1.0 - offset);

      /* flip the slider value, if necessary */
      if (paint_options->gradient_options->gradient_reverse)
        value = 1.0 - value;

      if (fabs (value - seg->middle) > EPSILON)
        {
          if (! changed)
            {
              gimp_blend_tool_editor_freeze_gradient (blend_tool);

              /* refetch the segment, since the gradient might have changed */
              seg = gimp_blend_tool_editor_handle_get_segment (blend_tool, i);

              changed = TRUE;
            }

          seg->middle = value;
        }
    }

  /* update the gradient stops */
  for (seg = blend_tool->gradient->segments, i = 0;
       seg->next;
       seg = seg->next, i++)
    {
      gdouble value;

      value = sliders[i].value;

      /* adjust slider value according to the offset */
      value = (value - offset) / (1.0 - offset);

      /* flip the slider value, if necessary */
      if (paint_options->gradient_options->gradient_reverse)
        value = 1.0 - value;

      if (fabs (value - seg->right) > EPSILON)
        {
          if (! changed)
            {
              gimp_blend_tool_editor_freeze_gradient (blend_tool);

              /* refetch the segment, since the gradient might have changed */
              seg = gimp_blend_tool_editor_handle_get_segment (blend_tool, i);

              changed = TRUE;
            }

          gimp_gradient_segment_range_compress (blend_tool->gradient,
                                                seg, seg,
                                                seg->left, value);
          gimp_gradient_segment_range_compress (blend_tool->gradient,
                                                seg->next, seg->next,
                                                value, seg->next->right);
        }
    }

  if (changed)
    gimp_blend_tool_editor_thaw_gradient (blend_tool);

  gimp_blend_tool_editor_update_gui (blend_tool);
}

void
gimp_blend_tool_editor_gradient_dirty (GimpBlendTool *blend_tool)
{
  if (gimp_blend_tool_editor_are_handlers_blocked (blend_tool))
    return;

  if (blend_tool->widget)
    {
      gimp_blend_tool_editor_update_sliders (blend_tool);

      gimp_tool_line_set_selection (GIMP_TOOL_LINE (blend_tool->widget),
                                    GIMP_TOOL_LINE_HANDLE_NONE);
    }
}

void
gimp_blend_tool_editor_gradient_changed (GimpBlendTool *blend_tool)
{
  GimpBlendOptions *options = GIMP_BLEND_TOOL_GET_OPTIONS (blend_tool);
  GimpContext      *context = GIMP_CONTEXT (options);

  if (options->modify_active_frame)
    {
      gtk_widget_set_sensitive (options->modify_active_frame,
                                blend_tool->gradient !=
                                gimp_gradients_get_custom (context->gimp));
    }

  if (options->modify_active_hint)
    {
      gtk_widget_set_visible (options->modify_active_hint,
                              blend_tool->gradient &&
                              ! gimp_data_is_writable (GIMP_DATA (blend_tool->gradient)));
    }

  if (gimp_blend_tool_editor_are_handlers_blocked (blend_tool))
    return;

  if (blend_tool->widget)
    {
      gimp_blend_tool_editor_update_sliders (blend_tool);

      gimp_tool_line_set_selection (GIMP_TOOL_LINE (blend_tool->widget),
                                    GIMP_TOOL_LINE_HANDLE_NONE);
    }
}
