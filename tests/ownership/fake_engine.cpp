// Models the engine side of the 4D Plugin API ownership contract, behind the
// real SDK (4DPluginAPI.c) and its gCall4D boundary:
// - EX_GET_OBJ_VALUE returns an owned copy (new Unistring / retained ref / picture)
// - EX_SET_OBJ_VALUE deep-copies; the caller keeps ownership of its variable
// - EX_CLEAR_VARIABLE releases what a variable owns
// - pictures are reference counted (PA_DuplicatePicture(p, 1) retains)
// - CONVERT PICTURE borrows its format text and owns the picture variable
// Every engine allocation is a real heap block, so ASan sees double frees and
// use-after-free; leaks are counted explicitly (LSan is not available on macOS).
// Adapted from miyako/4d-plugin-ical-v4 tests/ownership/fake_engine.cpp.
#include "fake_engine.h"
#include "EntryPoints.h"
#include "PrivateTypes.h"
#include <ImageIO/ImageIO.h>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace fake4d {

struct Pic {
    int rc = 1;
    std::vector<unsigned char> bytes;
};

struct Value {
    PA_VariableKind kind = eVK_Undefined;
    double real = 0;
    bool boolean = false;
    std::u16string text;
    Obj *ref = nullptr;
    Pic *pic = nullptr;
};

struct Obj {
    int rc = 1;
    bool collection = false;
    std::map<std::u16string, Value> props;
    std::vector<Value> elems;
};

static std::set<PA_Unichar *> gStrings;
static std::set<Obj *> gObjects;
static std::set<void *> gHandles;
static std::set<Pic *> gPictures;
static std::vector<std::string> gViolations;
static int gFailGets = 0;
static bool gCompiled = false;
static bool gConvertInPlace = false;
static int gConvertCalls = 0;
static Pic *const kPoison = (Pic *)(uintptr_t)0xBADBAD0;

static void violation(const std::string &s) { gViolations.push_back(s); }

static std::u16string u16(const char *s) { return std::u16string(s, s + strlen(s)); }

// EngineBlock and PA_Variable are packed: always take Unistrings by value.
static std::u16string from_unistring(PA_Unistring u) {
    if (!u.fString) return std::u16string();
    return std::u16string((const char16_t *)u.fString, (size_t)u.fLength);
}

static PA_Unistring make_unistring(const std::u16string &s) {
    PA_Unichar *p = new PA_Unichar[s.size() + 1];
    memcpy(p, s.data(), s.size() * sizeof(PA_Unichar));
    p[s.size()] = 0;
    gStrings.insert(p);
    PA_Unistring u = {(PA_long32)s.size(), p, 0, 0};
    return u;
}

static void dispose_unistring(PA_Unistring u) {
    PA_Unichar *p = u.fString;
    if (!p) return;
    auto it = gStrings.find(p);
    if (it == gStrings.end()) { violation("dispose_unknown_string"); return; }
    gStrings.erase(it);
    delete[] p;
}

static Pic *make_picture(const std::vector<unsigned char> &bytes) {
    Pic *p = new Pic;
    p->bytes = bytes;
    gPictures.insert(p);
    return p;
}

static bool live_picture(Pic *p, const char *what) {
    if (p == kPoison) { violation(std::string(what) + "_poison_picture"); return false; }
    if (!gPictures.count(p)) { violation(std::string(what) + "_unknown_picture"); return false; }
    return true;
}

static void dispose_picture(Pic *p) {
    if (!p) return;
    if (p == kPoison) { violation("cleared_value_after_get_error"); return; }
    if (!gPictures.count(p)) { violation("dispose_unknown_picture"); return; }
    if (--p->rc == 0) { gPictures.erase(p); delete p; }
}

static void retain(Obj *o) {
    if (!gObjects.count(o)) { violation("retain_dead_object"); return; }
    o->rc++;
}

static void release_value(Value &v) {
    if (v.ref) release(v.ref);
    v.ref = nullptr;
    if (v.pic) dispose_picture(v.pic);
    v.pic = nullptr;
}

void release(Obj *o) {
    if (!o) return;
    if (!gObjects.count(o)) { violation("release_dead_object"); return; }
    if (--o->rc == 0) {
        for (auto &kv : o->props) release_value(kv.second);
        for (auto &e : o->elems) release_value(e);
        gObjects.erase(o);
        delete o;
    }
}

static void clear_variable(PA_Variable *v) {
    switch ((unsigned char)v->fType) {
        case eVK_Unistring: dispose_unistring(v->uValue.fString); break;
        case eVK_Object:
        case eVK_Collection:
            if (v->uValue.fObject) release((Obj *)v->uValue.fObject);
            break;
        case eVK_Picture: dispose_picture((Pic *)v->uValue.fPicture); break;
        default: break;
    }
    v->fType = eVK_Undefined;
    memset(&v->uValue, 0, sizeof(v->uValue));
}

static void write_value(PA_Variable *out, const Value &val) {
    out->fFiller = 0;
    out->fType = (char)val.kind;
    switch (val.kind) {
        case eVK_Real: out->uValue.fReal = val.real; break;
        case eVK_Boolean: out->uValue.fBoolean = val.boolean; break;
        case eVK_Unistring: out->uValue.fString = make_unistring(val.text); break;
        case eVK_Picture: out->uValue.fPicture = make_picture(val.pic->bytes); break;
        case eVK_Object:
        case eVK_Collection: retain(val.ref); out->uValue.fObject = val.ref; break;
        default: out->fType = eVK_Undefined; break;
    }
}

static void get_value(EngineBlock *eb) {
    PA_Variable *out = (PA_Variable *)eb->fPtr2;
    Obj *o = (Obj *)eb->fPtr1;
    if (!gObjects.count(o)) { violation("get_from_dead_object"); eb->fError = -1; return; }
    bool element = eb->fShort == eVK_Collection;
    if (element && eb->fLongint == -1) { // PA_GetCollectionLength
        out->fType = eVK_Longint;
        out->uValue.fLongint = (PA_long32)o->elems.size();
        return;
    }
    if (gFailGets > 0) {
        gFailGets--;
        // The SDK's local `PA_Variable value;` is uninitialised on failure;
        // model it as a value the plugin must neither own nor clear.
        out->fType = eVK_Picture;
        out->fFiller = 0;
        out->uValue.fPicture = (PA_Picture)kPoison;
        eb->fError = -1;
        return;
    }
    if (element) {
        if (!o->collection) { violation("element_get_on_object"); eb->fError = -1; return; }
        if (eb->fLongint < 0 || (size_t)eb->fLongint >= o->elems.size()) { out->fType = eVK_Undefined; return; }
        write_value(out, o->elems[(size_t)eb->fLongint]);
        return;
    }
    auto it = o->props.find(from_unistring(eb->fUniString1));
    if (it == o->props.end()) { out->fType = eVK_Undefined; return; }
    write_value(out, it->second);
}

static void set_value(EngineBlock *eb) {
    Obj *o = (Obj *)eb->fPtr1;
    if (!gObjects.count(o)) { violation("set_on_dead_object"); return; }
    const PA_Variable *in = (const PA_Variable *)eb->fPtr2;
    Value val;
    int kind = (unsigned char)in->fType; // eVK_Null (255) does not fit a signed char
    if (kind == eVK_Null) kind = eVK_Undefined;
    val.kind = (PA_VariableKind)kind;
    switch (kind) {
        case eVK_Real: val.real = in->uValue.fReal; break;
        case eVK_Longint: val.kind = eVK_Real; val.real = in->uValue.fLongint; break;
        case eVK_Boolean: val.boolean = in->uValue.fBoolean; break;
        case eVK_Unistring:
            {
                PA_Unichar *p = in->uValue.fString.fString;
                if (p && !gStrings.count(p)) violation("set_unknown_string");
            }
            val.text = from_unistring(in->uValue.fString);
            break;
        case eVK_Picture:
            {
                Pic *p = (Pic *)in->uValue.fPicture;
                if (!live_picture(p, "set")) return;
                val.pic = make_picture(p->bytes); // deep copy
            }
            break;
        case eVK_Object:
        case eVK_Collection:
            val.ref = (Obj *)in->uValue.fObject;
            if (val.ref) retain(val.ref);
            break;
        case eVK_Undefined: break;
        default: violation("set_unsupported_kind"); return;
    }
    Value *slot;
    if (eb->fShort == eVK_Collection) {
        if (!o->collection || eb->fLongint < 0) { violation("bad_element_set"); release_value(val); return; }
        if ((size_t)eb->fLongint >= o->elems.size()) o->elems.resize((size_t)eb->fLongint + 1);
        slot = &o->elems[(size_t)eb->fLongint];
    } else {
        slot = &o->props[from_unistring(eb->fUniString1)];
    }
    release_value(*slot);
    *slot = val;
}

static void convert_picture(EngineBlock *eb, PA_Variable **params) {
    gConvertCalls++;
    if (eb->fParam2 != 2 || (unsigned char)params[0]->fType != eVK_Picture ||
        (unsigned char)params[1]->fType != eVK_Unistring) {
        violation("bad_convert_picture_params");
        return;
    }
    PA_Unichar *fmt = params[1]->uValue.fString.fString;
    if (!fmt || !gStrings.count(fmt)) violation("convert_picture_unknown_string");
    Pic *p = (Pic *)params[0]->uValue.fPicture;
    if (!live_picture(p, "convert")) return;
    if (!gConvertInPlace) {
        // the converted picture replaces the variable's value, which is released
        Pic *converted = make_picture(p->bytes);
        dispose_picture(p);
        params[0]->uValue.fPicture = (PA_Picture)converted;
    }
}

static void execute_command(EngineBlock *eb) {
    PA_Variable **params = (PA_Variable **)eb->fPtr1;
    PA_Variable *result = (PA_Variable *)eb->fPtr2;
    switch (eb->fParam1) {
        case 1231: { // OB Is defined
            Obj *o = (Obj *)params[0]->uValue.fObject;
            bool defined = gObjects.count(o) &&
                o->props.count(from_unistring(params[1]->uValue.fString));
            result->fType = eVK_Boolean;
            result->uValue.fBoolean = defined;
            return;
        }
        case 1471: // New object
        case 1472: // New collection
            result->fType = eb->fParam1 == 1471 ? eVK_Object : eVK_Collection;
            result->uValue.fObject = (PA_ObjectRef)new_object(eb->fParam1 == 1472);
            return;
        case 1002: // CONVERT PICTURE (no return value)
            convert_picture(eb, params);
            return;
    }
    violation("unexpected_command_" + std::to_string((long)eb->fParam1));
}

struct Handle { size_t size; char *data; };

static void handle_manager(EngineBlock *eb) {
    Handle *h = (Handle *)eb->fHandle;
    switch (eb->fParam1) {
        case 1: {
            h = new Handle{(size_t)eb->fParam2, (char *)calloc(1, (size_t)eb->fParam2 + 1)};
            gHandles.insert(h);
            eb->fHandle = (PA_Handle)h;
            break;
        }
        case 2:
            if (!gHandles.erase(h)) { violation("dispose_unknown_handle"); break; }
            free(h->data); delete h;
            break;
        case 3:
            h->data = (char *)realloc(h->data, (size_t)eb->fParam2 + 1);
            h->size = (size_t)eb->fParam2;
            eb->fParam1 = 1;
            break;
        case 4: eb->fParam2 = (sLONG_PTR)h->size; break;
        case 5: eb->fParam3 = (sLONG_PTR)h->data; break;
        case 6: case 7: case 8: break;
        default: violation("unexpected_handle_op");
    }
}

static void get_picture_data(EngineBlock *eb) {
    Pic *p = (Pic *)eb->fPicture;
    eb->fUniString1.fLength = 0; // format description is not modelled
    eb->fUniString1.fString = 0;
    if (!live_picture(p, "get_data")) { eb->fError = -1; return; }
    if (eb->fParam1 != 1) { eb->fError = -1; return; }
    Handle *h = (Handle *)eb->fHandle;
    if (!h) return;
    if (!gHandles.count(h)) { violation("get_data_unknown_handle"); eb->fError = -1; return; }
    h->data = (char *)realloc(h->data, p->bytes.size() + 1);
    h->size = p->bytes.size();
    memcpy(h->data, p->bytes.data(), p->bytes.size());
}

static void native_picture(EngineBlock *eb) {
    Pic *p = (Pic *)eb->fPicture;
    eb->fHandle = 0;
    if (!live_picture(p, "native")) { eb->fError = -1; return; }
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, p->bytes.data(), (CFIndex)p->bytes.size());
    CGImageSourceRef src = CGImageSourceCreateWithData(data, NULL);
    if (src) {
        eb->fHandle = (PA_Handle)CGImageSourceCreateImageAtIndex(src, 0, NULL); // caller releases
        CFRelease(src);
    }
    CFRelease(data);
}

static void call4d(short selector, EngineBlock *eb) {
    switch (selector) {
        case EX_GET_INFORMATION:
            eb->fParam1 = gCompiled ? 0x08000000 : 0; // PA_IsCompiled(0)
            eb->fParam2 = 0x00001900;                 // PA_Get4DVersion: v19+
            break;
        case EX_CREATE_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t n = 0;
            while (src && src[n]) n++;
            eb->fUniString1 = make_unistring(std::u16string((const char16_t *)src, n));
            break;
        }
        case EX_DISPOSE_UNISTRING: dispose_unistring(eb->fUniString1); break;
        case EX_CREATE_PICTURE: {
            const unsigned char *b = (const unsigned char *)eb->fPtr1;
            std::vector<unsigned char> bytes;
            if (b && eb->fParam1 > 0) bytes.assign(b, b + eb->fParam1);
            eb->fPicture = (PA_Picture)make_picture(bytes);
            break;
        }
        case EX_DISPOSE_PICTURE: dispose_picture((Pic *)eb->fPicture); break;
        case EX_DUPLICATE_PICTURE: {
            Pic *p = (Pic *)eb->fPicture;
            if (!live_picture(p, "duplicate")) { eb->fPicture = 0; eb->fError = -1; break; }
            if (eb->fParam1 == 0) p->rc++; // retain only
            else eb->fPicture = (PA_Picture)make_picture(p->bytes);
            break;
        }
        case EX_GET_PICTURE_DATA: get_picture_data(eb); break;
        case EX_CREATE_NATIVE_PICTURE_FOR_SCREEN: native_picture(eb); break;
        case EX_VARIABLE_TO_STRING: { // borrowed view of the variable's own string
            const PA_Variable *v = (const PA_Variable *)eb->fHandle;
            if ((unsigned char)v->fType != eVK_Unistring) { eb->fError = -1; break; }
            PA_Unistring u = v->uValue.fString;
            PA_Unichar *p = u.fString; // packed struct: copy before binding a reference
            if (p && !gStrings.count(p)) violation("read_unknown_string");
            eb->fUniString1 = u;
            break;
        }
        case EX_GET_OBJ_VALUE: get_value(eb); break;
        case EX_SET_OBJ_VALUE: set_value(eb); break;
        case EX_CLEAR_VARIABLE: clear_variable((PA_Variable *)eb->fPtr1); break;
        case EX_EXECUTE_COMMAND_BY_ID: execute_command(eb); break;
        case EX_HANDLE_MANAGER: handle_manager(eb); break;
        default: violation("unexpected_selector_" + std::to_string(selector));
    }
}

void install() { gCall4D = (Call4DProcPtr)call4d; }

void reset() {
    gViolations.clear();
    gFailGets = 0;
    gCompiled = false;
    gConvertInPlace = false;
    gConvertCalls = 0;
}

Obj *new_object(bool collection) {
    Obj *o = new Obj;
    o->collection = collection;
    gObjects.insert(o);
    return o;
}

int refcount(Obj *o) { return gObjects.count(o) ? o->rc : 0; }

void put_real(Obj *o, const char *k, double v) { Value x; x.kind = eVK_Real; x.real = v; o->props[u16(k)] = x; }
void put_bool(Obj *o, const char *k, bool v) { Value x; x.kind = eVK_Boolean; x.boolean = v; o->props[u16(k)] = x; }
void put_string(Obj *o, const char *k, const char *v) { Value x; x.kind = eVK_Unistring; x.text = u16(v); o->props[u16(k)] = x; }
void put_ref(Obj *o, const char *k, Obj *c) {
    Value x; x.kind = c->collection ? eVK_Collection : eVK_Object; x.ref = c; retain(c); o->props[u16(k)] = x;
}
void put_picture(Obj *o, const char *k) {
    Value x; x.kind = eVK_Picture; x.pic = make_picture({1, 2, 3}); o->props[u16(k)] = x;
}

bool get_bool(Obj *o, const char *k, bool *out) {
    auto it = o->props.find(u16(k));
    if (it == o->props.end() || it->second.kind != eVK_Boolean) return false;
    *out = it->second.boolean;
    return true;
}

bool get_string(Obj *o, const char *k, std::string *out) {
    auto it = o->props.find(u16(k));
    if (it == o->props.end() || it->second.kind != eVK_Unistring) return false;
    out->assign(it->second.text.begin(), it->second.text.end());
    return true;
}

bool has_picture(Obj *o, const char *k) {
    auto it = o->props.find(u16(k));
    return it != o->props.end() && it->second.kind == eVK_Picture && it->second.pic;
}

Obj *get_ref(Obj *o, const char *k) {
    auto it = o->props.find(u16(k));
    return it == o->props.end() ? nullptr : it->second.ref;
}

size_t length(Obj *c) { return c->elems.size(); }

bool element_string(Obj *c, size_t i, std::string *out) {
    if (i >= c->elems.size() || c->elems[i].kind != eVK_Unistring) return false;
    out->assign(c->elems[i].text.begin(), c->elems[i].text.end());
    return true;
}

bool element_is_picture(Obj *c, size_t i) {
    return i < c->elems.size() && c->elems[i].kind == eVK_Picture && c->elems[i].pic &&
        !c->elems[i].pic->bytes.empty();
}

Obj *element_ref(Obj *c, size_t i) { return i < c->elems.size() ? c->elems[i].ref : nullptr; }

PA_Picture new_picture(const std::vector<unsigned char> &bytes) { return (PA_Picture)make_picture(bytes); }

int picture_refcount(PA_Picture p) { return gPictures.count((Pic *)p) ? ((Pic *)p)->rc : 0; }

size_t live_strings() { return gStrings.size(); }
size_t live_handles() { return gHandles.size(); }
size_t live_objects() { return gObjects.size(); }
size_t live_pictures() {
    size_t n = 0;
    for (Pic *p : gPictures) n += (size_t)p->rc;
    return n;
}
void set_compiled(bool compiled) { gCompiled = compiled; }
void set_convert_in_place(bool in_place) { gConvertInPlace = in_place; }
int convert_calls() { return gConvertCalls; }
void fail_next_gets(int n) { gFailGets = n; }
const std::vector<std::string> &violations() { return gViolations; }
}
