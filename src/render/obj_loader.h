// Wavefront .obj reader for the Preview's gem models, plus Onyx's two built-in
// shapes. A port of how Onyx loads its models (Codec.Wavefront + its own
// triangulation and vertex sort), so the verbatim-copied .obj files in
// assets/preview/models draw with the same triangles in the same order.
//
// Output is a plain non-indexed triangle list of {position, normal, uv}
// vertices — the layout Onyx uploads (3 + 3 + 2 floats). Device-free and
// unit-tested; the renderer uploads it to a vertex buffer.

#ifndef HYDRA_RENDER_OBJ_LOADER_H
#define HYDRA_RENDER_OBJ_LOADER_H

#include <cstddef>
#include <string_view>
#include <vector>

namespace hydra::render {

struct ObjVertex {
    float pos[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    float uv[2] = {0, 0};
};

// A triangle list: vertices.size() is a multiple of 3.
struct ObjMesh {
    std::vector<ObjVertex> vertices;
    size_t triangle_count() const { return vertices.size() / 3; }
};

// Parse .obj text. Reads v / vt / vn / f; ignores mtllib, usemtl, o, g, s and
// comments (the Onyx models reference .mtl files that do not ship). Faces are
// fan-triangulated the way Onyx does ([v1,v2,v3], then [v1,v3,v4], ...). A
// face corner with no vt or vn gets zeros. Indices may be negative (relative).
// The result is sorted far-first (see sort_far_first). Throws
// std::runtime_error when the text has no faces or an index is out of range.
ObjMesh load_obj(std::string_view text);

// Onyx's `sortVertices`: stable-sort the triangles by the sum of their three
// Z coordinates, ascending — most negative Z (farthest up the highway) first,
// so alpha-blended texels of one model draw back to front.
void sort_far_first(ObjMesh& mesh);

// Onyx's `Flat`: a unit square in the X-Z plane at Y = 0 with a +Y normal,
// x and z in [-0.5, 0.5]. UV (0,0) sits at (-x, +z); u grows with +x, v with
// -z. Two triangles, counter-clockwise seen from +Y.
ObjMesh make_flat_quad();

// Onyx's `Box`: the unit cube [-0.5, 0.5]^3 with only its top (+Y), left (-X),
// front (+Z) and right (+X) faces — the four a camera above and in front can
// see. Per-face normals, counter-clockwise seen from outside. Used for the
// railings, which draw in a flat colour, so the UVs are a plain 0..1 per face.
ObjMesh make_box();

// Axis-aligned bounds of the mesh positions (both zero for an empty mesh).
void mesh_bounds(const ObjMesh& mesh, float min[3], float max[3]);

}  // namespace hydra::render

#endif  // HYDRA_RENDER_OBJ_LOADER_H
