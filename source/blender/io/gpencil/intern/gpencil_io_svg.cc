/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup bgpencil
 */
#include <iostream>
#include <iterator>
#include <list>
#include <string>

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_gpencil.h"
#include "BKE_gpencil_geom.h"
#include "BKE_main.h"
#include "BKE_material.h"

#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "DNA_gpencil_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "DEG_depsgraph.h"
#include "DEG_depsgraph_query.h"

#include "ED_gpencil.h"

#ifdef WIN32
#  include "utfconv.h"
#endif

#include "UI_view2d.h"

#include "ED_view3d.h"

#include "gpencil_io_exporter.h"
#include "gpencil_io_svg.h"

#include "pugixml.hpp"

namespace blender {
namespace io {
namespace gpencil {

/* Constructor. */
GpencilExporterSVG::GpencilExporterSVG(const struct GpencilExportParams *params)
    : GpencilExporter(params)
{
  this->invert_axis[0] = false;
  this->invert_axis[1] = true;
}

/* Main write method for SVG format. */
bool GpencilExporterSVG::write(std::string actual_frame)
{
  create_document_header();

  export_style_list();
  export_layers();

  /* Add frame to filename. */
  std::string frame_file = out_filename;
  size_t found = frame_file.find_last_of(".", 0);
  if (found != std::string::npos) {
    frame_file.replace(found, 8, actual_frame + ".svg");
  }

  return doc.save_file(frame_file.c_str());
}

/* Create document header and main svg node. */
void GpencilExporterSVG::create_document_header(void)
{
  /* Add a custom document declaration node. */
  pugi::xml_node decl = doc.prepend_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";

  pugi::xml_node comment = doc.append_child(pugi::node_comment);
  comment.set_value(SVG_EXPORTER_VERSION);

  pugi::xml_node doctype = doc.append_child(pugi::node_doctype);
  doctype.set_value(
      "svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
      "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\"");

  main_node = doc.append_child("svg");
  main_node.append_attribute("version").set_value("1.0");
  main_node.append_attribute("x").set_value("0px");
  main_node.append_attribute("y").set_value("0px");

  std::string width = std::to_string(winx) + "px";
  std::string height = std::to_string(winy) + "px";
  main_node.append_attribute("width").set_value(width.c_str());
  main_node.append_attribute("height").set_value(height.c_str());
  std::string viewbox = "0 0 " + std::to_string(winx) + " " + std::to_string(winy);
  main_node.append_attribute("viewBox").set_value(viewbox.c_str());
}

/**
 * Create Styles (materials) list.
 */
void GpencilExporterSVG::export_style_list(void)
{
  main_node.append_child(pugi::node_comment).set_value("List of materials");
  pugi::xml_node style_node = main_node.append_child("style");
  style_node.append_attribute("type").set_value("text/css");
  std::string txt;

  int ob_idx = 1;
  std::list<Object *>::iterator it;
  for (it = ob_list.begin(); it != ob_list.end(); ++it) {
    Object *ob = (Object *)*it;
    int mat_len = max_ii(1, ob->totcol);

    float col[3];

    for (int i = 0; i < mat_len; i++) {
      MaterialGPencilStyle *gp_style = BKE_gpencil_material_settings(ob, i + 1);
      gp_style_current_set(gp_style);

      int id = i + 1;

      if (gp_style_is_stroke()) {
        char out[128];
        linearrgb_to_srgb_v3_v3(col, gp_style->stroke_rgba);
        std::string stroke_hex = rgb_to_hex(col);
        sprintf(out,
                "\n\t.ob%dstylestroke%d{stroke: %s; fill: %s;}",
                ob_idx,
                id,
                stroke_hex.c_str(),
                stroke_hex.c_str());
        txt.append(out);
      }

      if (gp_style_is_fill()) {
        char out[128];
        linearrgb_to_srgb_v3_v3(col, gp_style->fill_rgba);
        std::string stroke_hex = rgb_to_hex(col);
        sprintf(out,
                "\n\t.ob%dstylefill%d{stroke: %s; fill: %s; fill-opacity: %f}",
                ob_idx,
                id,
                stroke_hex.c_str(),
                stroke_hex.c_str(),
                gp_style->fill_rgba[3]);
        txt.append(out);
      }
    }
    ob_idx++;
  }
  txt.append("\n\t");
  style_node.text().set(txt.c_str());
}

/* Main layer loop. */
void GpencilExporterSVG::export_layers(void)
{
  int ob_idx = 0;
  std::list<Object *>::iterator it;
  for (it = ob_list.begin(); it != ob_list.end(); ++it) {
    Object *ob = (Object *)*it;

    ob_idx++;
    ob_idx_set(ob_idx);
    /* Use evaluated version to get strokes with modifiers. */
    Object *ob_eval_ = (Object *)DEG_get_evaluated_id(depsgraph, &ob->id);
    bGPdata *gpd_eval = (bGPdata *)ob_eval_->data;

    LISTBASE_FOREACH (bGPDlayer *, gpl, &gpd_eval->layers) {
      if (gpl->flag & GP_LAYER_HIDE) {
        continue;
      }
      gpl_current_set(gpl);

      /* Layer node. */
      std::string txt = "Layer: ";
      txt.append(gpl->info);
      main_node.append_child(pugi::node_comment).set_value(txt.c_str());
      pugi::xml_node gpl_node = main_node.append_child("g");
      gpl_node.append_attribute("id").set_value(gpl->info);

      bGPDframe *gpf = gpl->actframe;
      if (gpf == NULL) {
        continue;
      }
      gpf_current_set(gpf);

      BKE_gpencil_parent_matrix_get(depsgraph, ob, gpl, diff_mat);

      LISTBASE_FOREACH (bGPDstroke *, gps, &gpf->strokes) {
        if (gps->totpoints == 0) {
          continue;
        }
        /* Duplicate the stroke to apply any layer thickness change. */
        bGPDstroke *gps_duplicate = BKE_gpencil_stroke_duplicate(gps, true);

        gps_current_set(ob, gps_duplicate);

        /* Apply layer thickness change. */
        gps_duplicate->thickness += gpl->line_change;
        CLAMP_MIN(gps_duplicate->thickness, 1.0f);

        if (gps_duplicate->totpoints == 1) {
          export_point(gpl_node);
        }
        else {
          bool is_normalized = ((params.flag & GP_EXPORT_NORM_THICKNESS) != 0);

          /* Fill. */
          if ((gp_style_is_fill()) && (params.flag & GP_EXPORT_FILL)) {
            if (is_normalized) {
              export_stroke_polyline(gpl_node, true);
            }
            else {
              export_stroke_path(gpl_node, true);
            }
          }

          /* Stroke. */
          if (gp_style_is_stroke()) {
            if (is_normalized) {
              export_stroke_polyline(gpl_node, false);
            }
            else {
              bGPDstroke *gps_perimeter = BKE_gpencil_stroke_perimeter_from_view(
                  rv3d, gpd, gpl, gps_duplicate, 3, diff_mat);

              gps_current_set(ob, gps_perimeter);

              /* Sample stroke. */
              BKE_gpencil_stroke_sample(gps_perimeter, 0.03f, false);

              export_stroke_path(gpl_node, false);

              BKE_gpencil_free_stroke(gps_perimeter);
            }
          }
        }

        BKE_gpencil_free_stroke(gps_duplicate);
      }
    }
  }
}

/**
 * Export a point
 * \param gpl_node: Node of the layer.
 * \param gps: Stroke to export.
 * \param diff_mat: Transformation matrix.
 */
void GpencilExporterSVG::export_point(pugi::xml_node gpl_node)
{
  bGPDlayer *gpl = gpl_current_get();
  bGPDstroke *gps = gps_current_get();
  MaterialGPencilStyle *gp_style = gp_style_current_get();

  BLI_assert(gps->totpoints == 1);
  float screen_co[2];

  pugi::xml_node gps_node = gpl_node.append_child("circle");

  if (gpl_current_get()->tintcolor[3] == 0.0f) {
    gps_node.append_attribute("class").set_value(
        ("ob" + std::to_string(ob_idx_get()) + "stylestroke" + std::to_string(gps->mat_nr + 1))
            .c_str());

    gps_node.append_attribute("stroke-opacity").set_value(gp_style->stroke_rgba[3] * gpl->opacity);
    gps_node.append_attribute("fill-opacity").set_value(gp_style->stroke_rgba[3] * gpl->opacity);
  }
  else {
    color_string_set(gps_node, false);
  }

  bGPDspoint *pt = &gps->points[0];
  gpencil_3d_point_to_screen_space(&pt->x, screen_co);

  gps_node.append_attribute("cx").set_value(screen_co[0]);
  gps_node.append_attribute("cy").set_value(screen_co[1]);

  /* Radius. */
  float radius = stroke_point_radius_get(gps);
  gps_node.append_attribute("r").set_value(radius);
}

/**
 * Export a stroke using path
 * \param gpl_node: Node of the layer.
 * \param gps: Stroke to export.
 * \param diff_mat: Transformation matrix.
 * \param is_fill: True if the stroke is only fill
 */
void GpencilExporterSVG::export_stroke_path(pugi::xml_node gpl_node, const bool is_fill)
{
  bGPDlayer *gpl = gpl_current_get();
  bGPDstroke *gps = gps_current_get();
  MaterialGPencilStyle *gp_style = gp_style_current_get();

  pugi::xml_node gps_node = gpl_node.append_child("path");

  std::string style_type = (is_fill) ? "fill" : "stroke";
  /* If the layer doesn't tint, can use the class. */
  if (gpl_current_get()->tintcolor[3] == 0.0f) {
    gps_node.append_attribute("class").set_value(("ob" + std::to_string(ob_idx_get()) + "style" +
                                                  style_type + std::to_string(gps->mat_nr + 1))
                                                     .c_str());
  }
  else {
    float col[3];
    std::string stroke_hex;
    if (is_fill) {
      gps_node.append_attribute("stroke-opacity").set_value(gp_style->fill_rgba[3] * gpl->opacity);
      gps_node.append_attribute("fill-opacity").set_value(gp_style->fill_rgba[3] * gpl->opacity);

      interp_v3_v3v3(col, gp_style->fill_rgba, gpl->tintcolor, gpl->tintcolor[3]);
      linearrgb_to_srgb_v3_v3(col, col);
      stroke_hex = rgb_to_hex(col);
    }
    else {
      gps_node.append_attribute("stroke-opacity")
          .set_value(gp_style->stroke_rgba[3] * gpl->opacity);
      gps_node.append_attribute("fill-opacity").set_value(gp_style->stroke_rgba[3] * gpl->opacity);

      interp_v3_v3v3(col, gp_style->stroke_rgba, gpl->tintcolor, gpl->tintcolor[3]);
      linearrgb_to_srgb_v3_v3(col, col);
      stroke_hex = rgb_to_hex(col);
    }
    gps_node.append_attribute("stroke").set_value(stroke_hex.c_str());
    gps_node.append_attribute("fill").set_value(stroke_hex.c_str());
  }

  gps_node.append_attribute("stroke-width").set_value("1.0");

  std::string txt = "M";
  for (int i = 0; i < gps->totpoints; i++) {
    if (i > 0) {
      txt.append("L");
    }
    bGPDspoint *pt = &gps->points[i];
    float screen_co[2];
    gpencil_3d_point_to_screen_space(&pt->x, screen_co);
    txt.append(std::to_string(screen_co[0]) + "," + std::to_string(screen_co[1]));
  }
  /* Close patch (cyclic)*/
  if (gps->flag & GP_STROKE_CYCLIC) {
    txt.append("z");
  }

  gps_node.append_attribute("d").set_value(txt.c_str());
}

/**
 * Export a stroke using polyline or polygon
 * \param gpl_node: Node of the layer.
 * \param gps: Stroke to export.
 * \param diff_mat: Transformation matrix.
 * \param is_fill: True if the stroke is only fill
 */
void GpencilExporterSVG::export_stroke_polyline(pugi::xml_node gpl_node, const bool is_fill)
{
  bGPDstroke *gps = gps_current_get();

  const bool is_thickness_const = is_stroke_thickness_constant(gps);
  const bool cyclic = ((gps->flag & GP_STROKE_CYCLIC) != 0);

  bGPDspoint *pt = &gps->points[0];
  float avg_pressure = pt->pressure;
  if (!is_thickness_const) {
    avg_pressure = stroke_average_pressure_get(gps);
  }

  /* Get the thickness in pixels using a simple 1 point stroke. */
  bGPDstroke *gps_temp = BKE_gpencil_stroke_duplicate(gps, false);
  gps_temp->totpoints = 1;
  gps_temp->points = (bGPDspoint *)MEM_callocN(sizeof(bGPDspoint), "gp_stroke_points");
  bGPDspoint *pt_src = &gps->points[0];
  bGPDspoint *pt_dst = &gps_temp->points[0];
  copy_v3_v3(&pt_dst->x, &pt_src->x);
  pt_dst->pressure = avg_pressure;

  float radius = stroke_point_radius_get(gps_temp);

  BKE_gpencil_free_stroke(gps_temp);

  pugi::xml_node gps_node = gpl_node.append_child(is_fill || cyclic ? "polygon" : "polyline");

  color_string_set(gps_node, is_fill);

  float thickness = is_fill ? 1.0f : radius;
  gps_node.append_attribute("stroke-width").set_value(thickness);

  std::string txt;
  for (int i = 0; i < gps->totpoints; i++) {
    if (i > 0) {
      txt.append(" ");
    }
    bGPDspoint *pt = &gps->points[i];
    float screen_co[2];
    gpencil_3d_point_to_screen_space(&pt->x, screen_co);
    txt.append(std::to_string(screen_co[0]) + "," + std::to_string(screen_co[1]));
  }

  gps_node.append_attribute("points").set_value(txt.c_str());
}

void GpencilExporterSVG::color_string_set(pugi::xml_node gps_node, const bool is_fill)
{
  bGPDlayer *gpl = gpl_current_get();
  bGPDstroke *gps = gps_current_get();
  MaterialGPencilStyle *gp_style = gp_style_current_get();

  const bool round_cap = (gps->caps[0] == GP_STROKE_CAP_ROUND ||
                          gps->caps[1] == GP_STROKE_CAP_ROUND);

  float col[3];
  if (is_fill) {
    interp_v3_v3v3(col, gp_style->fill_rgba, gpl->tintcolor, gpl->tintcolor[3]);
    linearrgb_to_srgb_v3_v3(col, col);
    std::string stroke_hex = rgb_to_hex(col);
    gps_node.append_attribute("fill").set_value(stroke_hex.c_str());
    gps_node.append_attribute("stroke").set_value("none");
  }
  else {
    interp_v3_v3v3(col, gp_style->stroke_rgba, gpl->tintcolor, gpl->tintcolor[3]);
    linearrgb_to_srgb_v3_v3(col, col);
    std::string stroke_hex = rgb_to_hex(col);
    gps_node.append_attribute("stroke").set_value(stroke_hex.c_str());

    if (gps->totpoints > 1) {
      gps_node.append_attribute("fill").set_value("none");
      gps_node.append_attribute("stroke-linecap").set_value(round_cap ? "round" : "square");
    }
    else {
      gps_node.append_attribute("fill").set_value(stroke_hex.c_str());
    }
  }
  gps_node.append_attribute("stroke-opacity").set_value(gp_style->stroke_rgba[3] * gpl->opacity);
  gps_node.append_attribute("fill-opacity").set_value(gp_style->fill_rgba[3] * gpl->opacity);
}

}  // namespace gpencil
}  // namespace io
}  // namespace blender