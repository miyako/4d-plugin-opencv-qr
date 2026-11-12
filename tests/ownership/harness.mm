// Ownership regressions for the opencv-qr plugin's 4D picture, string,
// variable and object handling. Unity build of the real plugin source plus the
// real SDK, C_TEXT.cpp and support/4DPlugin-JSON.cpp, running against
// tests/ownership/fake_engine instead of 4D. Input pictures are synthetic QR
// codes generated with OpenCV; no camera or user data is used.
// Usage: harness [expected-failure ...]  (names of tests that must fail)
#include "4DPlugin-opencv-qr.cpp"
#include "fake_engine.h"
#include "opencv2/imgproc.hpp"
#include <cstdio>
#include <functional>
#include <set>
#include <string>

using namespace fake4d;

static std::vector<std::string> gFailures;
#define CHECK(cond) do { if (!(cond)) gFailures.push_back(#cond); } while (0)

// white canvas with one QR code per text, side by side
static std::vector<unsigned char> qr_png(std::initializer_list<const char *> texts) {
    const int cell = 8, quiet = 4 * cell, gap = 6 * cell;
    std::vector<cv::Mat> codes;
    int width = quiet, height = 0;
    for (const char *t : texts) {
        cv::Mat qr, big;
        cv::QRCodeEncoder::create()->encode(t, qr);
        cv::resize(qr, big, cv::Size(), cell, cell, cv::INTER_NEAREST);
        codes.push_back(big);
        width += big.cols + gap;
        height = std::max(height, big.rows);
    }
    cv::Mat canvas(height + 2 * quiet, width - gap + quiet, CV_8UC1, cv::Scalar(255));
    int x = quiet;
    for (auto &c : codes) { c.copyTo(canvas(cv::Rect(x, quiet, c.cols, c.rows))); x += c.cols + gap; }
    cv::Mat bgr;
    cv::cvtColor(canvas, bgr, cv::COLOR_GRAY2BGR);
    std::vector<unsigned char> png;
    cv::imencode(".png", bgr, png);
    return png;
}

// opencv decode qrcode / opencv detect qrcode (&P;&8):J through PluginMain
static Obj *call(PA_long32 selector, PA_Picture picture, double eps = 0) {
    void *params[2] = {&picture, &eps};
    PA_ObjectRef result = NULL;
    PluginBlock block = {params, &result, NULL};
    PluginMain(selector, &block);
    return (Obj *)result;
}

static bool succeeded(Obj *status) {
    bool ok = false;
    return get_bool(status, "success", &ok) && ok;
}

static void check_corners(Obj *status, size_t codes) {
    Obj *corners = get_ref(status, "corners");
    CHECK(corners && length(corners) == 4 * codes);
    if (corners && length(corners)) CHECK(element_ref(corners, 0) != nullptr);
}

static void detect(bool compiled, bool in_place) {
    set_compiled(compiled);
    set_convert_in_place(in_place);
    PA_Picture pic = new_picture(qr_png({"alpha"}));
    Obj *status = call(2, pic);
    CHECK(status && succeeded(status));
    if (status) check_corners(status, 1);
    CHECK(convert_calls() == (compiled ? 1 : 0));
    CHECK(picture_refcount(pic) == 1); // the caller's picture is borrowed
    release(status);
    PA_DisposePicture(pic);
}

struct Test { const char *name; std::function<void()> body; };

static const Test kTests[] = {
    // --- LIVE: opencv detect qrcode / opencv decode qrcode ---
    {"live_detect_interpreted_releases_duplicate", [] { detect(false, false); }},
    {"live_detect_compiled_convert_picture_released", [] { detect(true, false); }},
    {"live_detect_compiled_convert_in_place_released", [] { detect(true, true); }},
    {"live_decode_values_images_released", [] {
        PA_Picture pic = new_picture(qr_png({"alpha", "beta"}));
        Obj *status = call(1, pic);
        CHECK(status && succeeded(status));
        if (status) {
            Obj *values = get_ref(status, "values"), *images = get_ref(status, "images");
            std::set<std::string> texts;
            for (size_t i = 0; values && i < length(values); i++) {
                std::string s;
                if (element_string(values, i, &s)) texts.insert(s);
            }
            CHECK((texts == std::set<std::string>{"alpha", "beta"}));
            CHECK(images && length(images) == 2);
            for (size_t i = 0; images && i < length(images); i++) CHECK(element_is_picture(images, i));
            check_corners(status, 2);
        }
        release(status);
        PA_DisposePicture(pic);
    }},
    {"live_decode_compiled_released", [] {
        set_compiled(true);
        PA_Picture pic = new_picture(qr_png({"gamma"}));
        Obj *status = call(1, pic);
        CHECK(status && succeeded(status));
        std::string s;
        Obj *values = status ? get_ref(status, "values") : nullptr;
        CHECK(values && element_string(values, 0, &s) && s == "gamma");
        CHECK(picture_refcount(pic) == 1);
        release(status);
        PA_DisposePicture(pic);
    }},
    {"live_null_picture_error_released", [] {
        Obj *status = call(2, NULL);
        std::string error;
        CHECK(status && !succeeded(status));
        CHECK(status && get_string(status, "error", &error) && error == "no picture parameter given");
        release(status);
    }},
    {"live_unreadable_picture_error_released", [] {
        PA_Picture pic = new_picture({1, 2, 3});
        Obj *status = call(1, pic);
        std::string error;
        CHECK(status && !succeeded(status));
        CHECK(status && get_string(status, "error", &error) && error == "could not read image data");
        CHECK(picture_refcount(pic) == 1);
        release(status);
        PA_DisposePicture(pic);
    }},
    // --- DORMANT: support/4DPlugin-JSON.cpp helpers built but not called ---
    {"dormant_ob_set_p_released", [] {
        Obj *o = new_object();
        ob_set_p((PA_ObjectRef)o, L"p", new_picture({1, 2, 3})); // consumes the picture
        CHECK(has_picture(o, "p"));
        release(o);
    }},
    {"dormant_ob_set_s_utf8_key_released", [] {
        Obj *o = new_object();
        ob_set_s((PA_ObjectRef)o, "k", "v");
        std::string s;
        CHECK(get_string(o, "k", &s) && s == "v");
        release(o);
    }},
    {"dormant_ob_set_a_released", [] {
        Obj *o = new_object();
        ob_set_a((PA_ObjectRef)o, L"a", L"x");
        CUTF16String u;
        u.push_back('y');
        ob_set_a((PA_ObjectRef)o, L"b", &u);
        std::string a, b;
        CHECK(get_string(o, "a", &a) && a == "x");
        CHECK(get_string(o, "b", &b) && b == "y");
        release(o);
    }},
    {"dormant_ob_get_a_released", [] {
        Obj *o = new_object();
        put_string(o, "k", "hello");
        CUTF8String s;
        CHECK(ob_get_a((PA_ObjectRef)o, L"k", &s));
        CHECK(s == CUTF8String((const uint8_t *)"hello"));
        release(o);
    }},
    {"dormant_ob_get_c_borrowed", [] {
        Obj *o = new_object(), *c = new_object(true);
        put_ref(o, "c", c);
        CHECK(ob_get_c((PA_ObjectRef)o, L"c") == (PA_CollectionRef)c);
        CHECK(refcount(c) == 2); // test + parent: the parent keeps the borrowed ref alive
        release(c);
        release(o);
    }},
    // --- HARDENING: a failed property read must not be cleared ---
    {"hardening_ob_get_error_not_cleared", [] {
        Obj *o = new_object(), *c = new_object(true);
        put_string(o, "s", "x"); put_bool(o, "b", true); put_real(o, "n", 1); put_ref(o, "c", c);
        release(c);
        CUTF8String s;
        fail_next_gets(1); ob_get_a((PA_ObjectRef)o, L"s", &s);
        fail_next_gets(1); CHECK(!ob_get_b((PA_ObjectRef)o, L"b"));
        fail_next_gets(1); CHECK(ob_get_n((PA_ObjectRef)o, L"n") == 0);
        fail_next_gets(1); CHECK(ob_get_c((PA_ObjectRef)o, L"c") == nullptr);
        CHECK(s.empty());
        release(o);
    }},
    // --- CONTROLS: must pass before and after the fix ---
    {"control_qr_fixture_decodes", [] {
        std::vector<unsigned char> png = qr_png({"alpha", "beta"});
        cv::Mat mat = cv::imdecode(png, cv::IMREAD_COLOR);
        std::vector<std::string> info;
        std::vector<cv::Point> points;
        CHECK(cv::QRCodeDetector().detectAndDecodeMulti(mat, info, points) && info.size() == 2);
    }},
    {"control_live_setters_balanced", [] {
        Obj *o = new_object(), *coll = new_object(true);
        ob_set_b((PA_ObjectRef)o, L"b", true);
        ob_set_n((PA_ObjectRef)o, L"n", 2);
        ob_set_c((PA_ObjectRef)o, L"c", (PA_CollectionRef)coll); // consumes the caller's reference
        CHECK(refcount(coll) == 1);
        bool b = false;
        CHECK(get_bool(o, "b", &b) && b);
        release(o);
        CHECK(refcount(coll) == 0);
    }},
    {"control_unknown_selector_noop", [] {
        PA_Picture pic = new_picture({1});
        CHECK(call(3, pic) == nullptr);
        CHECK(picture_refcount(pic) == 1);
        PA_DisposePicture(pic);
    }},
    {"control_ob_get_b_n_values", [] {
        Obj *o = new_object();
        put_bool(o, "b", true); put_real(o, "n", 3); put_string(o, "s", "x");
        CHECK(ob_get_b((PA_ObjectRef)o, L"b"));
        CHECK(ob_get_n((PA_ObjectRef)o, L"n") == 3);
        CHECK(!ob_get_b((PA_ObjectRef)o, L"missing"));
        release(o);
    }},
};

int main(int argc, char **argv) {
    install();
    std::set<std::string> expected(argv + 1, argv + argc);
    int unexpected = 0, passed = 0, xfailed = 0;
    for (const Test &t : kTests) {
        reset();
        gFailures.clear();
        size_t strings = live_strings(), handles = live_handles(), objects = live_objects(),
               pictures = live_pictures();
        @autoreleasepool { t.body(); }
        for (auto &v : violations()) gFailures.push_back("engine: " + v);
        if (live_strings() != strings)
            gFailures.push_back("leaked strings: " + std::to_string((long)(live_strings() - strings)));
        if (live_handles() != handles) gFailures.push_back("leaked handles");
        if (live_objects() != objects)
            gFailures.push_back("leaked objects: " + std::to_string((long)(live_objects() - objects)));
        if (live_pictures() != pictures)
            gFailures.push_back("leaked pictures: " + std::to_string((long)(live_pictures() - pictures)));
        bool failed = !gFailures.empty();
        bool want = expected.erase(t.name) > 0;
        const char *verdict = failed ? (want ? "XFAIL" : "FAIL") : (want ? "XPASS" : "PASS");
        printf("%-5s %s\n", verdict, t.name);
        for (auto &f : gFailures) printf("      %s\n", f.c_str());
        if (failed != want) unexpected++;
        else if (failed) xfailed++;
        else passed++;
    }
    for (auto &name : expected) { printf("UNKNOWN expected failure %s\n", name.c_str()); unexpected++; }
    printf("passed=%d xfailed=%d unexpected=%d\n", passed, xfailed, unexpected);
    printf("%s\n", unexpected ? "RESULT: UNEXPECTED" : "RESULT: OK");
    return unexpected ? 1 : 0;
}
