// Tests for render/obj_loader: Onyx-compatible .obj parsing (fan triangulation,
// far-first sort), the two built-in shapes, and the real copied drum models.

#include "doctest.h"

#include <cmath>
#include <string>

#include "render/file_util.h"
#include "render/obj_loader.h"

#ifndef HYDRA_ASSET_DIR
#error "HYDRA_ASSET_DIR must be defined (see CMakeLists.txt)"
#endif

using namespace hydra::render;

namespace {

// Onyx's drum-kick.obj, verbatim (4 quads, per-face normals, s off).
const char kKickObj[] =
    "g default\n"
    "v 0.5 0.01 0.015\nv 0.5 0.01 -0.015\nv 0.5 0.0 0.015\nv 0.5 0.0 -0.015\n"
    "v -0.5 0.01 0.015\nv -0.5 0.01 -0.015\nv -0.5 0.0 0.015\nv -0.5 0.0 -0.015\n"
    "vt 0.0 1.0\nvt 1.0 1.0\nvt 0.0 0.5\nvt 1.0 0.5\nvt 0.0 0.5\n"
    "vt 0.0 0.0\nvt 0.0 0.0\nvt 1.0 0.0\nvt 1.0 0.5\nvt 1.0 0.0\n"
    "vn  0.0 1.0 0.0\nvn -1.0 0.0 0.0\nvn  0.0 0.0 1.0\nvn  1.0 0.0 0.0\n"
    "s off\ng box_mesh\n"
    "f 6/1/1 5/3/1 1/4/1 2/2/1\n"
    "f 6/5/2 8/6/2 7/7/2 5/3/2\n"
    "f 5/3/3 7/7/3 3/8/3 1/4/3\n"
    "f 1/4/4 3/8/4 4/10/4 2/9/4\n";

float zsum(const ObjMesh& m, size_t tri) {
    return m.vertices[tri * 3].pos[2] + m.vertices[tri * 3 + 1].pos[2] +
           m.vertices[tri * 3 + 2].pos[2];
}

}  // namespace

TEST_CASE("load_obj fan-triangulates quads and keeps per-corner attributes") {
    ObjMesh m = load_obj(kKickObj);
    CHECK(m.triangle_count() == 8);
    CHECK(m.vertices.size() == 24);
    // Every vertex carries a unit normal and a uv inside [0,1].
    for (const ObjVertex& v : m.vertices) {
        float len = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] +
                              v.normal[2] * v.normal[2]);
        CHECK(len == doctest::Approx(1.0f));
        CHECK(v.uv[0] >= 0.0f);
        CHECK(v.uv[0] <= 1.0f);
        CHECK(v.uv[1] >= 0.0f);
        CHECK(v.uv[1] <= 1.0f);
    }
    float mn[3], mx[3];
    mesh_bounds(m, mn, mx);
    CHECK(mn[0] == doctest::Approx(-0.5f));
    CHECK(mx[0] == doctest::Approx(0.5f));
    CHECK(mn[1] == doctest::Approx(0.0f));
    CHECK(mx[1] == doctest::Approx(0.01f));
    CHECK(mn[2] == doctest::Approx(-0.015f));
    CHECK(mx[2] == doctest::Approx(0.015f));
}

TEST_CASE("load_obj sorts triangles far-first (ascending z sum)") {
    ObjMesh m = load_obj(kKickObj);
    for (size_t i = 1; i < m.triangle_count(); ++i) CHECK(zsum(m, i - 1) <= zsum(m, i));
    // Fan order within a face: the quad 6 5 1 2 yields (6,5,1) and (6,1,2), so
    // vertex 6 (-0.5, 0.01, -0.015) starts both of its triangles. After the
    // sort those two triangles still exist with that first vertex.
    int tris_starting_at_v6 = 0;
    for (size_t t = 0; t < m.triangle_count(); ++t) {
        const ObjVertex& a = m.vertices[t * 3];
        if (a.pos[0] == -0.5f && a.pos[1] == 0.01f && a.pos[2] == -0.015f)
            ++tris_starting_at_v6;
    }
    CHECK(tris_starting_at_v6 >= 2);
}

TEST_CASE("load_obj handles v//vn, bare v, negative indices, and CRLF") {
    const char* text =
        "v 0 0 0\r\nv 1 0 0\r\nv 0 1 0\r\nvn 0 0 1\r\n"
        "f 1//1 2//1 3//1\r\n"
        "f -3 -2 -1\r\n";
    ObjMesh m = load_obj(text);
    CHECK(m.triangle_count() == 2);
    CHECK(m.vertices[0].normal[2] == doctest::Approx(1.0f));
    CHECK(m.vertices[3].normal[2] == doctest::Approx(0.0f));  // no vn on face 2
    CHECK(m.vertices[4].pos[0] == doctest::Approx(1.0f));
}

TEST_CASE("load_obj rejects text with no faces and bad indices") {
    CHECK_THROWS(load_obj("v 0 0 0\n"));
    CHECK_THROWS(load_obj("v 0 0 0\nf 1 2 3\n"));
}

TEST_CASE("make_flat_quad: unit square at y=0, +Y normal, Onyx UV layout") {
    ObjMesh m = make_flat_quad();
    CHECK(m.triangle_count() == 2);
    float mn[3], mx[3];
    mesh_bounds(m, mn, mx);
    CHECK(mn[0] == doctest::Approx(-0.5f));
    CHECK(mx[0] == doctest::Approx(0.5f));
    CHECK(mn[1] == doctest::Approx(0.0f));
    CHECK(mx[1] == doctest::Approx(0.0f));
    CHECK(mn[2] == doctest::Approx(-0.5f));
    CHECK(mx[2] == doctest::Approx(0.5f));
    for (const ObjVertex& v : m.vertices) {
        CHECK(v.normal[1] == doctest::Approx(1.0f));
        // u grows with +x, v grows with -z.
        CHECK(v.uv[0] == doctest::Approx(v.pos[0] + 0.5f));
        CHECK(v.uv[1] == doctest::Approx(0.5f - v.pos[2]));
    }
    // Counter-clockwise seen from +Y: the first triangle's normal via cross
    // product points up.
    const ObjVertex &a = m.vertices[0], &b = m.vertices[1], &c = m.vertices[2];
    float e1[3] = {b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
    float e2[3] = {c.pos[0] - a.pos[0], c.pos[1] - a.pos[1], c.pos[2] - a.pos[2]};
    float ny = e1[2] * e2[0] - e1[0] * e2[2];
    CHECK(ny > 0.0f);
}

TEST_CASE("make_box: four outward faces, no bottom or back") {
    ObjMesh m = make_box();
    CHECK(m.triangle_count() == 8);
    bool saw_top = false, saw_left = false, saw_front = false, saw_right = false;
    for (size_t t = 0; t < m.triangle_count(); ++t) {
        const ObjVertex &a = m.vertices[t * 3], &b = m.vertices[t * 3 + 1],
                        &c = m.vertices[t * 3 + 2];
        float e1[3] = {b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
        float e2[3] = {c.pos[0] - a.pos[0], c.pos[1] - a.pos[1], c.pos[2] - a.pos[2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                      e1[0] * e2[1] - e1[1] * e2[0]};
        // The winding normal agrees with the stored normal (outward).
        float dot = n[0] * a.normal[0] + n[1] * a.normal[1] + n[2] * a.normal[2];
        CHECK(dot > 0.0f);
        if (a.normal[1] > 0.5f) saw_top = true;
        if (a.normal[0] < -0.5f) saw_left = true;
        if (a.normal[2] > 0.5f) saw_front = true;
        if (a.normal[0] > 0.5f) saw_right = true;
        CHECK(a.normal[1] > -0.5f);  // no bottom
        CHECK(a.normal[2] > -0.5f);  // no back
    }
    CHECK(saw_top);
    CHECK(saw_left);
    CHECK(saw_front);
    CHECK(saw_right);
}

TEST_CASE("the copied Onyx drum models load with their known extents") {
    const std::string dir = std::string(HYDRA_ASSET_DIR) + "/models/";
    float mn[3], mx[3];

    ObjMesh kick = load_obj(read_file_text(dir + "drum-kick.obj"));
    CHECK(kick.triangle_count() == 8);

    ObjMesh tom = load_obj(read_file_text(dir + "drum-tom.obj"));
    // 126 quads + two 14-gons (the top and bottom rims) = 252 + 24 triangles.
    CHECK(tom.triangle_count() == 252 + 24);
    mesh_bounds(tom, mn, mx);
    CHECK(mn[0] == doctest::Approx(-0.491f).epsilon(0.01));
    CHECK(mx[0] == doctest::Approx(0.491f).epsilon(0.01));
    CHECK(mn[1] == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(mx[1] == doctest::Approx(0.309f).epsilon(0.01));
    CHECK(mx[2] == doctest::Approx(0.233f).epsilon(0.01));

    ObjMesh cymbal = load_obj(read_file_text(dir + "drum-cymbal.obj"));
    // 352 quads + 32 triangles + one 32-gon = 704 + 32 + 30 triangles.
    CHECK(cymbal.triangle_count() == 704 + 32 + 30);
    mesh_bounds(cymbal, mn, mx);
    CHECK(mn[1] == doctest::Approx(0.045f).epsilon(0.05));
    CHECK(mx[1] == doctest::Approx(0.270f).epsilon(0.02));
    CHECK(mx[0] == doctest::Approx(0.506f).epsilon(0.02));
}
