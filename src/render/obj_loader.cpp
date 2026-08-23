#include "render/obj_loader.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace hydra::render {

namespace {

struct Corner {
    long v = 0, vt = 0, vn = 0;  // 1-based as written; 0 = absent
};

// Split a line into whitespace-separated tokens.
std::vector<std::string_view> tokens_of(std::string_view line) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
        if (j > i) out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

float to_float(std::string_view s) {
    return std::strtof(std::string(s).c_str(), nullptr);
}

long to_long(std::string_view s) {
    if (s.empty()) return 0;
    return std::strtol(std::string(s).c_str(), nullptr, 10);
}

// "v/vt/vn", "v//vn", "v/vt", or "v".
Corner parse_corner(std::string_view tok) {
    Corner c;
    size_t a = tok.find('/');
    if (a == std::string_view::npos) {
        c.v = to_long(tok);
        return c;
    }
    c.v = to_long(tok.substr(0, a));
    size_t b = tok.find('/', a + 1);
    if (b == std::string_view::npos) {
        c.vt = to_long(tok.substr(a + 1));
        return c;
    }
    c.vt = to_long(tok.substr(a + 1, b - a - 1));
    c.vn = to_long(tok.substr(b + 1));
    return c;
}

// Resolve a 1-based (or negative relative) index against a list of `n`.
size_t resolve(long idx, size_t n, const char* what) {
    long r = idx > 0 ? idx - 1 : static_cast<long>(n) + idx;
    if (idx == 0 || r < 0 || static_cast<size_t>(r) >= n)
        throw std::runtime_error(std::string("obj: ") + what + " index out of range");
    return static_cast<size_t>(r);
}

}  // namespace

ObjMesh load_obj(std::string_view text) {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 2>> uvs;
    std::vector<std::array<float, 3>> normals;
    ObjMesh mesh;

    auto make_vertex = [&](const Corner& c) {
        ObjVertex v;
        const auto& p = positions[resolve(c.v, positions.size(), "vertex")];
        v.pos[0] = p[0];
        v.pos[1] = p[1];
        v.pos[2] = p[2];
        if (c.vt != 0 && !uvs.empty()) {
            const auto& t = uvs[resolve(c.vt, uvs.size(), "texcoord")];
            v.uv[0] = t[0];
            v.uv[1] = t[1];
        }
        if (c.vn != 0 && !normals.empty()) {
            const auto& n = normals[resolve(c.vn, normals.size(), "normal")];
            v.normal[0] = n[0];
            v.normal[1] = n[1];
            v.normal[2] = n[2];
        }
        return v;
    };

    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string_view line =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = nl == std::string_view::npos ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::vector<std::string_view> t = tokens_of(line);
        if (t.empty() || t[0][0] == '#') continue;
        if (t[0] == "v" && t.size() >= 4) {
            positions.push_back({to_float(t[1]), to_float(t[2]), to_float(t[3])});
        } else if (t[0] == "vt" && t.size() >= 3) {
            uvs.push_back({to_float(t[1]), to_float(t[2])});
        } else if (t[0] == "vn" && t.size() >= 4) {
            normals.push_back({to_float(t[1]), to_float(t[2]), to_float(t[3])});
        } else if (t[0] == "f" && t.size() >= 4) {
            // Onyx's triangulate: [v1,v2,v3] ++ triangulate (v1 : v3 : rest).
            std::vector<Corner> corners;
            for (size_t i = 1; i < t.size(); ++i) corners.push_back(parse_corner(t[i]));
            for (size_t i = 1; i + 1 < corners.size(); ++i) {
                mesh.vertices.push_back(make_vertex(corners[0]));
                mesh.vertices.push_back(make_vertex(corners[i]));
                mesh.vertices.push_back(make_vertex(corners[i + 1]));
            }
        }
        // mtllib / usemtl / o / g / s and anything else: ignored.
    }
    if (mesh.vertices.empty()) throw std::runtime_error("obj: no faces");
    sort_far_first(mesh);
    return mesh;
}

void sort_far_first(ObjMesh& mesh) {
    const size_t n = mesh.triangle_count();
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    auto zsum = [&](size_t tri) {
        const ObjVertex* v = &mesh.vertices[tri * 3];
        return v[0].pos[2] + v[1].pos[2] + v[2].pos[2];
    };
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return zsum(a) < zsum(b); });
    std::vector<ObjVertex> sorted;
    sorted.reserve(mesh.vertices.size());
    for (size_t tri : order)
        for (int k = 0; k < 3; ++k) sorted.push_back(mesh.vertices[tri * 3 + k]);
    mesh.vertices.swap(sorted);
}

namespace {

ObjVertex vtx(float x, float y, float z, float nx, float ny, float nz, float u, float v) {
    ObjVertex o;
    o.pos[0] = x;
    o.pos[1] = y;
    o.pos[2] = z;
    o.normal[0] = nx;
    o.normal[1] = ny;
    o.normal[2] = nz;
    o.uv[0] = u;
    o.uv[1] = v;
    return o;
}

// Two triangles for the quad a,b,c,d (fan from a), matching load_obj's order.
void push_quad(ObjMesh& m, const ObjVertex& a, const ObjVertex& b, const ObjVertex& c,
               const ObjVertex& d) {
    m.vertices.push_back(a);
    m.vertices.push_back(b);
    m.vertices.push_back(c);
    m.vertices.push_back(a);
    m.vertices.push_back(c);
    m.vertices.push_back(d);
}

}  // namespace

ObjMesh make_flat_quad() {
    ObjMesh m;
    // Counter-clockwise seen from +Y: (-x,+z) -> (+x,+z) -> (+x,-z) -> (-x,-z).
    push_quad(m, vtx(-0.5f, 0, 0.5f, 0, 1, 0, 0, 0), vtx(0.5f, 0, 0.5f, 0, 1, 0, 1, 0),
              vtx(0.5f, 0, -0.5f, 0, 1, 0, 1, 1), vtx(-0.5f, 0, -0.5f, 0, 1, 0, 0, 1));
    return m;
}

ObjMesh make_box() {
    ObjMesh m;
    const float h = 0.5f;
    // Top (+Y)
    push_quad(m, vtx(-h, h, h, 0, 1, 0, 0, 0), vtx(h, h, h, 0, 1, 0, 1, 0),
              vtx(h, h, -h, 0, 1, 0, 1, 1), vtx(-h, h, -h, 0, 1, 0, 0, 1));
    // Left (-X)
    push_quad(m, vtx(-h, -h, -h, -1, 0, 0, 0, 0), vtx(-h, -h, h, -1, 0, 0, 1, 0),
              vtx(-h, h, h, -1, 0, 0, 1, 1), vtx(-h, h, -h, -1, 0, 0, 0, 1));
    // Front (+Z)
    push_quad(m, vtx(-h, -h, h, 0, 0, 1, 0, 0), vtx(h, -h, h, 0, 0, 1, 1, 0),
              vtx(h, h, h, 0, 0, 1, 1, 1), vtx(-h, h, h, 0, 0, 1, 0, 1));
    // Right (+X)
    push_quad(m, vtx(h, -h, h, 1, 0, 0, 0, 0), vtx(h, -h, -h, 1, 0, 0, 1, 0),
              vtx(h, h, -h, 1, 0, 0, 1, 1), vtx(h, h, h, 1, 0, 0, 0, 1));
    return m;
}

void mesh_bounds(const ObjMesh& mesh, float min[3], float max[3]) {
    for (int k = 0; k < 3; ++k) min[k] = max[k] = 0.0f;
    if (mesh.vertices.empty()) return;
    for (int k = 0; k < 3; ++k) min[k] = max[k] = mesh.vertices[0].pos[k];
    for (const ObjVertex& v : mesh.vertices)
        for (int k = 0; k < 3; ++k) {
            min[k] = std::min(min[k], v.pos[k]);
            max[k] = std::max(max[k], v.pos[k]);
        }
}

}  // namespace hydra::render
