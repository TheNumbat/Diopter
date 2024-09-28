
#pragma once

#include <rpp/base.h>
#include <rpp/pool.h>
#include <rpp/variant.h>
#include <rpp/vmath.h>

using namespace rpp;

namespace PBRT {

using Alloc = Mallocator<"PBRT Parser">;

using Spectrum = Vec3;

template<typename T>
struct ID {
    u8 depth = RPP_UINT8_MAX;
    u64 id = RPP_UINT64_MAX;

    bool invalid() const {
        return depth == RPP_UINT8_MAX || id == RPP_UINT64_MAX;
    }
    ID clone() const {
        return *this;
    }
    bool operator==(const PBRT::ID<T>& b) const {
        return depth == b.depth && id == b.id;
    }
    bool operator!=(const PBRT::ID<T>& b) const {
        return depth != b.depth || id != b.id;
    }
};

template<typename T>
struct Image_Data {
    Vec<T, Alloc> data;
    u32 w = 0;
    u32 h = 0;
    u32 channels = 0;
};

namespace Lights {

struct Light;
using Light_ID = ID<Light>;

enum class Type : u8 {
    distant,
    goniometric,
    infinite,
    point,
    projection,
    spot,
};

struct Light {
    Type type = Type::point;

    Spectrum scale = Spectrum{1.0f};
    f32 power = 0.0f;
    f32 illuminance = 0.0f;
    Spectrum L;
    Vec3 from;
    Vec3 to = Vec3{0.0f, 0.0f, 1.0f};
    Spectrum I;
    Vec3 portal[4];
    f32 fov = 90.0f;
    f32 cone_angle = 30.0f;
    f32 cone_delta_angle = 5.0f;
    Image_Data<f32> map;
};

} // namespace Lights

using Lights::Light;
using Lights::Light_ID;

namespace Textures {

struct Texture;
using Texture_ID = ID<Texture>;

enum class Type : u8 {
    bilerp,
    checkerboard,
    constant,
    directionmix,
    dots,
    fbm,
    imagemap,
    marble,
    mix,
    ptex,
    scale,
    windy,
    wrinkled
};
enum class Map : u8 { uv, spherical, cylindrical, planar };
enum class Data : u8 { scalar, spectrum };
enum class Wrap : u8 { repeat, clamp, black };
enum class Filter : u8 { point, bilinear, trilinear, ewa };
enum class Encoding : u8 { sRGB, linear, gamma };

struct Texture {
    Type type = Type::constant;
    Data data_type = Data::scalar;

    f32 scalar = 1.0f;
    Spectrum spectrum{1.0f};

    Map map = Map::uv;
    Wrap wrap = Wrap::repeat;
    Filter filter = Filter::bilinear;
    Encoding encoding = Encoding::sRGB;
    f32 gamma = 2.2f;
    bool invert = false;

    f32 u_scale = 1.0f;
    f32 v_scale = 1.0f;
    f32 u_delta = 0.0f;
    f32 v_delta = 0.0f;
    f32 roughness = 0.5f;
    f32 variation = 0.2f;
    f32 max_anisotropy = 8.0f;
    i32 octaves = 8;
    i32 dimension = 2;

    Vec3 v1 = Vec3{1.0f, 0.0f, 0.0f};
    Vec3 v2 = Vec3{0.0f, 1.0f, 0.0f};
    Vec3 dir = Vec3{0.0f, 1.0f, 0.0f};

    Texture_ID v00;
    Texture_ID v01;
    Texture_ID v10;
    Texture_ID v11;
    Texture_ID tex1;
    Texture_ID tex2;
    Texture_ID inside;
    Texture_ID outside;
    Texture_ID amount;
    Texture_ID tex;
    Texture_ID scale;

    Variant<Image_Data<u8>, Image_Data<f32>> image = Image_Data<u8>{};
};

} // namespace Textures

using Textures::Texture;
using Textures::Texture_ID;

namespace Materials {

struct Material;
using Material_ID = ID<Material>;

enum class Type : u8 {
    conductor,
    dielectric,
    diffuse,
    diffuse_transmission,
    mix,
    coated_diffuse,
    coated_conductor,
    hair,
    interface,
    measured,
    subsurface,
    thin_dielectric
};

struct Material {
    Type type = Type::diffuse;
    bool remap_roughness = true;
    i32 max_depth = 6;
    i32 n_samples = 1;
    String<Alloc> normal_map;
    String<Alloc> measured;
    String<Alloc> sss_coefficients;
    Spectrum color;
    Texture_ID roughness;
    Texture_ID uroughness;
    Texture_ID vroughness;
    Texture_ID albedo;
    Texture_ID g;
    Texture_ID sigma_a;
    Texture_ID displacement_map;
    Texture_ID reflectance;
    Texture_ID transmittance;
    Texture_ID eumelanin;
    Texture_ID pheomelanin;
    Texture_ID beta_m;
    Texture_ID beta_n;
    Texture_ID alpha;
    Texture_ID eta;
    Texture_ID k;
    Texture_ID scale;
    Texture_ID amount;
    Texture_ID mfp;
    Texture_ID sigma_s;
    Texture_ID conductor_eta;
    Texture_ID conductor_k;
    Texture_ID conductor_roughness;
    Texture_ID conductor_uroughness;
    Texture_ID conductor_vroughness;
    Texture_ID interface_eta;
    Texture_ID interface_k;
    Texture_ID interface_roughness;
    Texture_ID interface_uroughness;
    Texture_ID interface_vroughness;
    Texture_ID thickness;
    Material_ID a;
    Material_ID b;
};

} // namespace Materials

using Materials::Material;
using Materials::Material_ID;

struct Mesh;
struct Object;

using Mesh_ID = ID<Mesh>;
using Object_ID = ID<Object>;

struct Mesh {
    Mat4 mesh_to_instance;
    Material_ID material;
    Texture_ID alpha;
    Spectrum emission;

    Vec<f32, Alloc> positions;
    Vec<f32, Alloc> normals;
    Vec<f32, Alloc> tangents;
    Vec<f32, Alloc> uvs;
    Vec<u32, Alloc> indices;
    Vec<u32, Alloc> face_indices;

    void reverse_orientation();
};

struct Camera {
    Mat4 world_to_camera;
};

struct Instance {
    Mat4 instance_to_object;
    Object_ID object;
};

struct Object {
    Mat4 object_to_parent;
    Vec<Mesh_ID, Alloc> meshes;
    Vec<Instance, Alloc> instances;
};

struct Scene {
    Camera camera;

    Vec<Mesh_ID, Alloc> top_level_meshes;
    Vec<Instance, Alloc> top_level_instances;

    Vec<Mesh, Alloc> meshes;
    Vec<Object, Alloc> objects;
    Vec<Material, Alloc> materials;
    Vec<Texture, Alloc> textures;
    Vec<Light, Alloc> lights;
};

Async::Task<Scene> load(Async::Pool<>& pool, String_View file);

} // namespace PBRT

namespace rpp::Hash {

template<typename T>
struct Hash<PBRT::ID<T>> {
    static u64 hash(PBRT::ID<T> id) {
        return rpp::hash(id.depth, id.id);
    }
};

} // namespace rpp::Hash

template<typename T>
RPP_TEMPLATE_RECORD(PBRT::ID, T, RPP_FIELD(depth), RPP_FIELD(id));

RPP_ENUM(PBRT::Lights::Type, distant, RPP_CASE(distant), RPP_CASE(goniometric), RPP_CASE(infinite),
         RPP_CASE(point), RPP_CASE(projection), RPP_CASE(spot));

RPP_ENUM(PBRT::Textures::Type, bilerp, RPP_CASE(bilerp), RPP_CASE(checkerboard), RPP_CASE(constant),
         RPP_CASE(directionmix), RPP_CASE(dots), RPP_CASE(fbm), RPP_CASE(imagemap),
         RPP_CASE(marble), RPP_CASE(mix), RPP_CASE(ptex), RPP_CASE(scale), RPP_CASE(windy),
         RPP_CASE(wrinkled));

RPP_ENUM(PBRT::Textures::Map, uv, RPP_CASE(uv), RPP_CASE(spherical), RPP_CASE(cylindrical),
         RPP_CASE(planar));

RPP_ENUM(PBRT::Textures::Data, scalar, RPP_CASE(scalar), RPP_CASE(spectrum));

RPP_ENUM(PBRT::Textures::Wrap, repeat, RPP_CASE(repeat), RPP_CASE(clamp), RPP_CASE(black));

RPP_ENUM(PBRT::Textures::Filter, point, RPP_CASE(point), RPP_CASE(bilinear), RPP_CASE(trilinear),
         RPP_CASE(ewa));

RPP_NAMED_ENUM(PBRT::Textures::Encoding, "PBRT::Textures::Enc", sRGB, RPP_CASE(sRGB),
               RPP_CASE(linear), RPP_CASE(gamma));

RPP_NAMED_RECORD(PBRT::Textures::Texture, "PBRT::Texture", RPP_FIELD(type), RPP_FIELD(data_type),
                 RPP_FIELD(scalar), RPP_FIELD(spectrum), RPP_FIELD(map), RPP_FIELD(wrap),
                 RPP_FIELD(filter), RPP_FIELD(encoding), RPP_FIELD(gamma), RPP_FIELD(invert),
                 RPP_FIELD(u_scale), RPP_FIELD(v_scale), RPP_FIELD(u_delta), RPP_FIELD(v_delta),
                 RPP_FIELD(roughness), RPP_FIELD(variation), RPP_FIELD(max_anisotropy),
                 RPP_FIELD(octaves), RPP_FIELD(dimension), RPP_FIELD(v1), RPP_FIELD(v2),
                 RPP_FIELD(dir), RPP_FIELD(v00), RPP_FIELD(v01), RPP_FIELD(v10), RPP_FIELD(v11),
                 RPP_FIELD(tex1), RPP_FIELD(tex2), RPP_FIELD(inside), RPP_FIELD(outside),
                 RPP_FIELD(amount), RPP_FIELD(tex), RPP_FIELD(scale), RPP_FIELD(image));

RPP_ENUM(PBRT::Materials::Type, diffuse, RPP_CASE(diffuse), RPP_CASE(diffuse_transmission),
         RPP_CASE(mix), RPP_CASE(coated_diffuse), RPP_CASE(coated_conductor), RPP_CASE(hair),
         RPP_CASE(interface), RPP_CASE(measured), RPP_CASE(subsurface), RPP_CASE(thin_dielectric));

RPP_NAMED_RECORD(PBRT::Materials::Material, "PBRT::Material", RPP_FIELD(type),
                 RPP_FIELD(remap_roughness), RPP_FIELD(max_depth), RPP_FIELD(n_samples),
                 RPP_FIELD(normal_map), RPP_FIELD(measured), RPP_FIELD(sss_coefficients),
                 RPP_FIELD(color), RPP_FIELD(roughness), RPP_FIELD(uroughness),
                 RPP_FIELD(vroughness), RPP_FIELD(albedo), RPP_FIELD(g), RPP_FIELD(sigma_a),
                 RPP_FIELD(displacement_map), RPP_FIELD(reflectance), RPP_FIELD(transmittance),
                 RPP_FIELD(eumelanin), RPP_FIELD(pheomelanin), RPP_FIELD(beta_m), RPP_FIELD(beta_n),
                 RPP_FIELD(alpha), RPP_FIELD(eta), RPP_FIELD(k), RPP_FIELD(scale),
                 RPP_FIELD(amount), RPP_FIELD(mfp), RPP_FIELD(sigma_s), RPP_FIELD(conductor_eta),
                 RPP_FIELD(conductor_k), RPP_FIELD(conductor_roughness), RPP_FIELD(interface_eta),
                 RPP_FIELD(interface_k), RPP_FIELD(interface_roughness), RPP_FIELD(thickness),
                 RPP_FIELD(a), RPP_FIELD(b));

RPP_RECORD(PBRT::Mesh, RPP_FIELD(mesh_to_instance), RPP_FIELD(material), RPP_FIELD(alpha),
           RPP_FIELD(positions), RPP_FIELD(normals), RPP_FIELD(tangents), RPP_FIELD(uvs),
           RPP_FIELD(indices), RPP_FIELD(face_indices));

RPP_RECORD(PBRT::Camera, RPP_FIELD(world_to_camera));

RPP_RECORD(PBRT::Instance, RPP_FIELD(instance_to_object), RPP_FIELD(object));

RPP_RECORD(PBRT::Object, RPP_FIELD(object_to_parent), RPP_FIELD(meshes), RPP_FIELD(instances));

RPP_RECORD(PBRT::Scene, RPP_FIELD(camera), RPP_FIELD(top_level_meshes),
           RPP_FIELD(top_level_instances), RPP_FIELD(meshes), RPP_FIELD(objects),
           RPP_FIELD(materials), RPP_FIELD(textures));
