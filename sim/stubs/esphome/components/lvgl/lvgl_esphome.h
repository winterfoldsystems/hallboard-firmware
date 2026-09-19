// Host stand-in for ESPHome's LVGL glue. hb_ui.h uses one function from it,
// esphome::lvgl::lv_container_create, and this is the same thing ESPHome's lvgl_esphome.cpp does:
// an object of a private class whose only difference from lv_obj is that the default theme does
// not recognise it, so none of the theme's styles are applied and the panel starts bare.
#pragma once
#include <lvgl.h>
#include <lvgl_private.h>

namespace esphome::lvgl {

inline void hb_container_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
  LV_UNUSED(class_p);
  LV_UNUSED(obj);
}

// Built once, lazily, because a namespace-scope lv_obj_class_t would need a C++20 designated
// initialiser to match ESPHome's. Every field ESPHome leaves out is zero here too, which is what
// makes lv_obj_class_create_obj fall back to the base class for the instance size.
inline const lv_obj_class_t *hb_container_class() {
  static const lv_obj_class_t cls = [] {
    lv_obj_class_t c = {};
    c.base_class = &lv_obj_class;
    c.constructor_cb = hb_container_constructor;
    c.name = "lv_container";
    return c;
  }();
  return &cls;
}

inline lv_obj_t *lv_container_create(lv_obj_t *parent) {
  lv_obj_t *obj = lv_obj_class_create_obj(hb_container_class(), parent);
  lv_obj_class_init_obj(obj);
  return obj;
}

}  // namespace esphome::lvgl
