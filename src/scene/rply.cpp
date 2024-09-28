
#include <stdexcept>

#include <rply/rply.h>
#include <rpp/base.h>

#include "pbrt.h"

using namespace rpp;

namespace RPLY {

static i32 rply_vertex_callback_vec3(p_ply_argument argument) {
    f32* buffer;
    ply_get_argument_user_data(argument, (void**)&buffer, null);
    long index;
    ply_get_argument_element(argument, null, &index);
    f32 value = static_cast<f32>(ply_get_argument_value(argument));
    buffer[index * 3] = value;
    return 1;
}

static i32 rply_vertex_callback_vec2(p_ply_argument argument) {
    f32* buffer;
    ply_get_argument_user_data(argument, (void**)&buffer, null);
    long index;
    ply_get_argument_element(argument, null, &index);
    f32 value = static_cast<f32>(ply_get_argument_value(argument));
    buffer[index * 2] = value;
    return 1;
}

static i32 rply_face_callback(p_ply_argument argument) {
    i32* buffer;
    ply_get_argument_user_data(argument, (void**)&buffer, null);
    long length, value_index;
    ply_get_argument_property(argument, null, &length, &value_index);
    if(value_index == -1) {
        if(length != 3) throw std::runtime_error("Non-triangular face.");
        return 1;
    }
    long index;
    ply_get_argument_element(argument, null, &index);
    buffer[index * 3 + value_index] = static_cast<i32>(ply_get_argument_value(argument));
    return 1;
}

PBRT::Mesh load(String_View directory, String_View filename) {

    p_ply ply;
    Region(R) {
        auto path = directory.append<Mregion<R>>(filename).terminate<Mregion<R>>();
        ply = ply_open(reinterpret_cast<const char*>(path.data()), null, 0, null);

        if(!ply) {
            warn("PBRT: couldn't open PLY file: %", path);
            return PBRT::Mesh{};
        }
    }

    if(!ply_read_header(ply)) {
        warn("PBRT: unable to read the header of PLY file: %", filename);
        return PBRT::Mesh{};
    }

    PBRT::Mesh mesh;
    long vertex_count = 0;
    long face_count = 0;
    bool has_normals = false;
    bool has_uvs = false;
    bool has_indices = false;
    const char* tex_coord_u_name = null;
    const char* tex_coord_v_name = null;
    const char* vertex_indices_name = null;

    p_ply_element element = null;
    while((element = ply_get_next_element(ply, element)) != null) {
        const char* name_;
        long instance_count;
        ply_get_element_info(element, &name_, &instance_count);

        String_View name{name_};

        if(name == "vertex"_v) {

            vertex_count = instance_count;

            bool has_position_components[3] = {false};
            bool has_normal_components[3] = {false};
            bool has_uv_components[2] = {false};

            p_ply_property property = null;
            while((property = ply_get_next_property(element, property)) != null) {
                const char* pname_;
                ply_get_property_info(property, &pname_, null, null, null);

                String_View pname{pname_};

                if(pname == "x"_v)
                    has_position_components[0] = true;
                else if(pname == "y"_v)
                    has_position_components[1] = true;
                else if(pname == "z"_v)
                    has_position_components[2] = true;
                else if(pname == "nx"_v)
                    has_normal_components[0] = true;
                else if(pname == "ny"_v)
                    has_normal_components[1] = true;
                else if(pname == "nz"_v)
                    has_normal_components[2] = true;
                else if(pname == "u"_v || pname == "s"_v || pname == "texture_u"_v ||
                        pname == "texture_s"_v) {
                    has_uv_components[0] = true;
                    tex_coord_u_name = pname_;
                } else if(pname == "v"_v || pname == "t"_v || pname == "texture_v"_v ||
                          pname == "texture_t"_v) {
                    has_uv_components[1] = true;
                    tex_coord_v_name = pname_;
                }
            }

            if(!(has_position_components[0] && has_position_components[1] &&
                 has_position_components[2])) {

                warn("PBRT: PLY: vertex coordinate property not found in file: %", filename);
                return PBRT::Mesh{};
            }

            has_normals =
                has_normal_components[0] && has_normal_components[1] && has_normal_components[2];
            has_uvs = has_uv_components[0] && has_uv_components[1];

        } else if(name == "face"_v) {

            face_count = instance_count;

            p_ply_property property = null;
            while((property = ply_get_next_property(element, property)) != null) {
                const char* pname_;
                ply_get_property_info(property, &pname_, null, null, null);

                String_View pname{pname_};

                if(pname != "vertex_index"_v || pname != "vertex_indices"_v) {
                    has_indices = true;
                    vertex_indices_name = pname_;
                }
            }
        }
    }

    if(vertex_count == 0 || face_count == 0) {
        warn("PBRT: PLY: No face/vertex elements found in file: %", filename);
        return PBRT::Mesh{};
    }

    mesh.positions.resize(vertex_count * 3);
    if(has_normals) mesh.normals.resize(vertex_count * 3);
    if(has_uvs) mesh.uvs.resize(vertex_count * 2);
    if(has_indices) mesh.indices.resize(face_count * 3);

    ply_set_read_cb(ply, "vertex", "x", rply_vertex_callback_vec3, &mesh.positions[0], 0);
    ply_set_read_cb(ply, "vertex", "y", rply_vertex_callback_vec3, &mesh.positions[1], 0);
    ply_set_read_cb(ply, "vertex", "z", rply_vertex_callback_vec3, &mesh.positions[2], 0);

    if(has_normals) {
        ply_set_read_cb(ply, "vertex", "nx", rply_vertex_callback_vec3, &mesh.normals[0], 0);
        ply_set_read_cb(ply, "vertex", "ny", rply_vertex_callback_vec3, &mesh.normals[1], 0);
        ply_set_read_cb(ply, "vertex", "nz", rply_vertex_callback_vec3, &mesh.normals[2], 0);
    }

    if(has_uvs) {
        ply_set_read_cb(ply, "vertex", tex_coord_u_name, rply_vertex_callback_vec2, &mesh.uvs[0],
                        0);
        ply_set_read_cb(ply, "vertex", tex_coord_v_name, rply_vertex_callback_vec2, &mesh.uvs[1],
                        0);
    }

    if(has_indices) {
        ply_set_read_cb(ply, "face", vertex_indices_name, rply_face_callback, mesh.indices.data(),
                        0);
    }

    try {
        if(!ply_read(ply)) {
            ply_close(ply);
            warn("PBRT: PLY: unable to read contents of file: %", filename);
            return PBRT::Mesh{};
        }
    } catch(std::runtime_error err) {
        warn("PBRT: PLY: got exception while reading file: % - %", filename,
             String_View{err.what()});
        return PBRT::Mesh{};
    }

    ply_close(ply);
    return mesh;
}

} // namespace RPLY
