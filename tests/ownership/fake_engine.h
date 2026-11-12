// Synthetic 4D engine behind the real SDK's gCall4D boundary.
#pragma once
#include "4DPluginAPI.h"
#include <string>
#include <vector>

namespace fake4d {
struct Obj;
void install();
void reset();
Obj *new_object(bool collection = false);
void release(Obj *o);
int refcount(Obj *o);
void put_real(Obj *o, const char *key, double v);
void put_bool(Obj *o, const char *key, bool v);
void put_string(Obj *o, const char *key, const char *v);
void put_ref(Obj *o, const char *key, Obj *child); // retains child
void put_picture(Obj *o, const char *key);
bool get_bool(Obj *o, const char *key, bool *out);
bool has_picture(Obj *o, const char *key);
bool get_string(Obj *o, const char *key, std::string *out);
Obj *get_ref(Obj *o, const char *key);  // borrowed
size_t length(Obj *c);
bool element_string(Obj *c, size_t i, std::string *out);
bool element_is_picture(Obj *c, size_t i);
Obj *element_ref(Obj *c, size_t i);     // borrowed
// pictures: reference-counted, carry encoded image bytes
PA_Picture new_picture(const std::vector<unsigned char> &bytes);
int picture_refcount(PA_Picture p);
size_t live_strings();
size_t live_handles();
size_t live_objects();
size_t live_pictures();
// PA_IsCompiled(0) result: selects CONVERT PICTURE vs PA_CreateNativePictureForScreen
void set_compiled(bool compiled);
// CONVERT PICTURE either replaces the variable's picture (old one released) or
// converts it in place (same reference kept)
void set_convert_in_place(bool in_place);
int convert_calls();
// next N property/element reads fail and write a poison value the plugin must
// neither use nor clear
void fail_next_gets(int n);
const std::vector<std::string> &violations();
}
