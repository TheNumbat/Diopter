
#include <stb/stb_image.h>
#include <stdexcept>
#include <tinyexr/tinyexr.h>

#include <rpp/asyncio.h>
#include <rpp/base.h>
#include <rpp/files.h>
#include <rpp/stack.h>

#include "pbrt.h"
#include "pbrt_const.h"
#include "rply.h"

#define BUILTIN_SPECTRUM(name) averaged_spectrum(Slice{name, sizeof(name) / sizeof(f64)})
#define LOAD_TEXTURES 1

using namespace rpp;

namespace rpp::ascii {

static bool is_special(u8 c) {
    return c == '[' || c == ']' || c == '"';
}

} // namespace rpp::ascii

namespace PBRT {

struct Parser {

    Parser(u8 depth) : scene_depth(depth) {
        state_stack.push(Graphics{});
    }
    ~Parser() = default;

    Parser(const Parser&) = delete;
    Parser& operator=(const Parser&) = delete;

    Parser(Parser&&) = default;
    Parser& operator=(Parser&&) = default;

    struct Error : std::exception {
        Error(u64 line, const char* msg) : line(line), msg(msg) {
        }
        u64 line = 0;
        const char* msg = null;
    };

    struct Area_Light {
        Spectrum L;
        bool two_sided = false;
        f32 scale = 1.0f;
    };

    struct Graphics {
        Mat4 transform;
        Variant<Material_ID, String<Alloc>> material{Material_ID{}};
        bool reverse_orientation = false;
        Area_Light area_light;

        Graphics clone() const {
            Graphics ret;
            ret.transform = transform;
            ret.material = material.clone();
            ret.reverse_orientation = reverse_orientation;
            ret.area_light = area_light;
            return ret;
        }
    };

    // These are only for parsing this file; they only contain local IDs.
    String<Alloc> directory;
    Stack<Graphics, Alloc> state_stack;
    Stack<Object_ID, Alloc> object_stack;

    u8 scene_depth = 0;

    // These get data from the parent, so the ID must record nesting depth for remapping.
    Map<String<Alloc>, Mat4, Alloc> named_transforms;
    Map<String<Alloc>, Object_ID, Alloc> named_objects;
    Map<String<Alloc>, Material_ID, Alloc> named_materials;
    Map<String<Alloc>, Texture_ID, Alloc> named_textures;

    bool world_begun = false;

    Mat4& current_transform() {
        return state_stack.top().transform;
    }
    bool& current_reverse_orientation() {
        return state_stack.top().reverse_orientation;
    }
    Area_Light& current_area_light() {
#ifdef RPP_COMPILER_MSVC // wtf
        Libc::keep_alive();
#endif
        return state_stack.top().area_light;
    }
    Opt<Object_ID> current_object() {
        if(object_stack.empty()) return {};
        return Opt<Object_ID>{object_stack.top()};
    }
    Variant<Material_ID, String<Alloc>>& current_material() {
        return state_stack.top().material;
    }
    void push_object(Object_ID obj_id) {
        object_stack.push(obj_id);
    }
    void pop_object() {
        object_stack.pop();
    }
    void push_state() {
        state_stack.push(state_stack.top().clone());
    }
    void pop_state() {
        state_stack.pop();
    }
    void reset_state() {
        state_stack.top() = Graphics{};
    }
    Graphics& current_state() {
        return state_stack.top();
    }

    void named_transform(String_View name_, Mat4 transform) {
        auto name = name_.string<Alloc>();
        named_transforms.insert(move(name), transform);
    }
    Mat4 named_transform(String_View name) {
        if(!named_transforms.contains(name)) throw Error{0, "Named transform not found."};
        return named_transforms.get(name);
    }

    void named_object(String_View name_, Object_ID id) {
        auto name = name_.string<Alloc>();
        named_objects.insert(move(name), id);
    }
    Object_ID named_object(String_View name) {
        if(auto obj = named_objects.try_get(name); obj.ok()) {
            return **obj;
        }
        warn("Failed to find named object %.", name);
        throw Error{0, "Named object not found."};
    }

    void named_material(String_View name_, Material_ID id) {
        auto name = name_.string<Alloc>();
        named_materials.insert(move(name), id);
    }
    Material_ID named_material(String_View name) {
        if(auto mat = named_materials.try_get(name); mat.ok()) {
            return **mat;
        }
        warn("Failed to find named material %.", name);
        throw Error{0, "Named material not found."};
    }

    void named_texture(String_View name_, Texture_ID id) {
        auto name = name_.string<Alloc>();
        named_textures.insert(move(name), id);
    }
    Texture_ID named_texture(String_View name) {
        if(auto tex = named_textures.try_get(name); tex.ok()) {
            return **tex;
        }
        warn("Failed to find named texture %.", name);
        throw Error{0, "Named texture not found."};
    }

    Parser fork() {
        Parser ret(scene_depth + 1);
        ret.directory = directory.clone();
        ret.world_begun = world_begun;
        ret.named_transforms = named_transforms.clone();
        ret.named_objects = named_objects.clone();
        ret.named_materials = named_materials.clone();
        ret.named_textures = named_textures.clone();
        return ret;
    }
};

struct Tokenizer {

    Tokenizer() = default;
    ~Tokenizer() = default;

    Tokenizer(const Tokenizer&) = delete;
    Tokenizer& operator=(const Tokenizer&) = delete;

    Tokenizer(Tokenizer&&) = default;
    Tokenizer& operator=(Tokenizer&&) = default;

    struct Token {
        u64 idx = 0;
        u64 length = 0;

        bool eof() {
            return length == 0;
        }
    };

    struct Ignore_Counts {
        u64 displacement = 0;
        u64 edgelength = 0;
        u64 bilinear_mesh = 0;
        u64 loop_subdiv_mesh = 0;
        u64 curve = 0;
        u64 cylinder = 0;
        u64 disk = 0;
        u64 sphere = 0;
        u64 make_named_medium = 0;
        u64 medium_interface = 0;
    };

    Vec<u8, Files::Alloc> file;
    u64 pos = 0;
    u64 line = 1;
    Ignore_Counts ignore;

    void eat() {
        while(pos < file.length()) {
            if(file[pos] == '#') {
                while(pos < file.length() && file[pos] != '\n') pos++;
                continue;
            }
            if(file[pos] == '\n') line++;
            if(!ascii::is_whitespace(file[pos])) break;
            pos++;
        }
    }

    Token next() {
        eat();
        Token token;
        token.idx = pos;
        if(pos < file.length() && ascii::is_special(file[pos])) {
            token.length = 1;
            pos++;
            return token;
        }
        while(pos < file.length()) {
            u8 c = file[pos];
            if(ascii::is_whitespace(c) || ascii::is_special(c)) break;
            pos++;
        }
        token.length = pos - token.idx;
        return token;
    }
    Token peek() {
        u64 old_pos = pos;
        u64 old_line = line;
        Token token = next();
        pos = old_pos;
        line = old_line;
        return token;
    }
    void skip() {
        next();
    }
    [[noreturn]] void fail(const char* msg) {
        throw Parser::Error{line, msg};
    }

    String_View to_string(Token token) {
        return String_View{file.data() + token.idx, token.length};
    }

    bool expect_bool() {
        Token token = next();
        if(is_string(token, "true")) return true;
        if(is_string(token, "false")) return false;
        fail("Expected boolean.");
    }
    u32 expect_int() {
        Token token = next();
        const char* str = reinterpret_cast<char*>(file.data()) + token.idx;
        char* end = null;
        i32 ret = std::strtol(str, &end, 10);
        if(end != str + token.length) fail("Expected integer.");
        if(ret < 0) fail("Expected positive integer.");
        return static_cast<u32>(ret);
    }
    f32 expect_float() {
        Token token = next();
        const char* str = reinterpret_cast<char*>(file.data()) + token.idx;
        char* end = null;
        f32 ret = std::strtof(str, &end);
        if(end != str + token.length) fail("Expected float.");
        return ret;
    }
    void expect_lbracket() {
        Token token = next();
        if(token.length != 1 || file[token.idx] != '[') {
            fail("Expected '['.");
        }
    }
    void expect_rbracket() {
        Token token = next();
        if(token.length != 1 || file[token.idx] != ']') {
            fail("Expected ']'.");
        }
    }
    void expect_quote() {
        Token token = next();
        if(token.length != 1 || file[token.idx] != '"') {
            fail("Expected '\"'.");
        }
    }
    Token expect_quoted_string() {
        expect_quote();
        Token ret;
        ret.idx = pos;
        while(pos < file.length()) {
            if(file[pos] == '"') break;
            if(file[pos] == '\n') line++;
            pos++;
        }
        ret.length = pos - ret.idx;
        expect_quote();
        return ret;
    }

    bool is_int(Token token) {
        const char* str = reinterpret_cast<char*>(file.data()) + token.idx;
        char* end = null;
        std::strtol(str, &end, 10);
        return end == str + token.length;
    }
    bool is_float(Token token) {
        const char* str = reinterpret_cast<char*>(file.data()) + token.idx;
        char* end = null;
        std::strtof(str, &end);
        return end == str + token.length;
    }
    bool is_lbracket(Token token) {
        return token.length == 1 && file[token.idx] == '[';
    }
    bool is_rbracket(Token token) {
        return token.length == 1 && file[token.idx] == ']';
    }
    bool is_quote(Token token) {
        return token.length == 1 && file[token.idx] == '"';
    }
    bool is_string(Token token, const char* str) {
        if(token.length != Libc::strlen(str)) return false;
        return Libc::memcmp(file.data() + token.idx, str, token.length) == 0;
    }

    bool type_is_int(Token token) {
        return is_string(token, "integer");
    }
    bool type_is_float(Token token) {
        return is_string(token, "float");
    }
    bool type_is_vec3(Token token) {
        return is_string(token, "vector3");
    }
    bool type_is_float_array(Token token) {
        return is_string(token, "float") || is_string(token, "point") ||
               is_string(token, "point2") || is_string(token, "point3") ||
               is_string(token, "normal3") || is_string(token, "vector3");
    }
    bool type_is_spectrum(Token token) {
        return is_string(token, "spectrum");
    }
    bool type_is_rgb(Token token) {
        return is_string(token, "rgb") || is_string(token, "color");
    }
    bool type_is_blackbody(Token token) {
        return is_string(token, "blackbody");
    }
    bool type_is_point(Token token) {
        return is_string(token, "point3") || is_string(token, "point");
    }
    bool type_is_bool(Token token) {
        return is_string(token, "bool");
    }
    bool type_is_string(Token token) {
        return is_string(token, "string");
    }
    bool type_is_texture(Token token) {
        return is_string(token, "texture");
    }
};

static Spectrum averaged_spectrum(Slice<const f64> data) {
    constexpr f64 red_nm = 700.0;
    constexpr f64 blue_nm = 475.0;
    constexpr f64 green_nm = 530.0;

    // Horrible approximation
    Spectrum ret;
    for(u64 i = 0; i < data.length() / 2 - 1; i++) {
        f64 x0 = data[2 * i];
        f64 y0 = data[2 * i + 1];
        f64 x1 = data[2 * (i + 1)];
        f64 y1 = data[2 * (i + 1) + 1];
        if(x0 <= red_nm && x1 >= red_nm) {
            ret.x = static_cast<f32>((y0 + y1) / 2.0);
        }
        if(x0 <= green_nm && x1 >= green_nm) {
            ret.y = static_cast<f32>((y0 + y1) / 2.0);
        }
        if(x0 <= blue_nm && x1 >= blue_nm) {
            ret.z = static_cast<f32>((y0 + y1) / 2.0);
        }
    }
    return ret;
};

static Spectrum builtin_blackbody(f64 temperature) {
    constexpr f64 red_nm = 700.0;
    constexpr f64 blue_nm = 475.0;
    constexpr f64 green_nm = 530.0;

    constexpr f64 c = 299792458.0;
    constexpr f64 h = 6.62606957e-34;
    constexpr f64 kb = 1.3806488e-23;

    auto planck = [temperature](f64 nm) {
        f64 l = nm * 1e-9;
        f64 l5 = l * l * l * l * l;
        return (2.0 * h * c * c) / (l5 * (Math::exp((h * c) / (l * kb * temperature)) - 1.0));
    };

    f64 lambda_max = 2.8977721e-3 / temperature;
    f64 norm = 1.0 / planck(lambda_max * 1e9);

    f32 r = static_cast<f32>(norm * planck(red_nm));
    f32 g = static_cast<f32>(norm * planck(green_nm));
    f32 b = static_cast<f32>(norm * planck(blue_nm));

    return Spectrum{r, g, b};
}

static Spectrum builtin_constant(Tokenizer& tokens, Tokenizer::Token builtin) {

    if(tokens.is_string(builtin, "glass-BK7")) {
        return BUILTIN_SPECTRUM(Const::GlassBK7_eta);
    } else if(tokens.is_string(builtin, "glass-BAF10")) {
        return BUILTIN_SPECTRUM(Const::GlassBAF10_eta);
    } else if(tokens.is_string(builtin, "glass-FK51A")) {
        return BUILTIN_SPECTRUM(Const::GlassFK51A_eta);
    } else if(tokens.is_string(builtin, "glass-LASF9")) {
        return BUILTIN_SPECTRUM(Const::GlassLASF9_eta);
    } else if(tokens.is_string(builtin, "glass-F5")) {
        return BUILTIN_SPECTRUM(Const::GlassSF5_eta);
    } else if(tokens.is_string(builtin, "glass-F10")) {
        return BUILTIN_SPECTRUM(Const::GlassSF5_eta);
    } else if(tokens.is_string(builtin, "glass-F11")) {
        return BUILTIN_SPECTRUM(Const::GlassSF11_eta);
    } else if(tokens.is_string(builtin, "metal-Ag-eta")) {
        return BUILTIN_SPECTRUM(Const::Ag_eta);
    } else if(tokens.is_string(builtin, "metal-Ag-k")) {
        return BUILTIN_SPECTRUM(Const::Ag_k);
    } else if(tokens.is_string(builtin, "metal-Al-eta")) {
        return BUILTIN_SPECTRUM(Const::Al_eta);
    } else if(tokens.is_string(builtin, "metal-Al-k")) {
        return BUILTIN_SPECTRUM(Const::Al_k);
    } else if(tokens.is_string(builtin, "metal-Au-eta")) {
        return BUILTIN_SPECTRUM(Const::Au_eta);
    } else if(tokens.is_string(builtin, "metal-Au-k")) {
        return BUILTIN_SPECTRUM(Const::Au_k);
    } else if(tokens.is_string(builtin, "metal-Cu-eta")) {
        return BUILTIN_SPECTRUM(Const::Cu_eta);
    } else if(tokens.is_string(builtin, "metal-Cu-k")) {
        return BUILTIN_SPECTRUM(Const::Cu_k);
    } else if(tokens.is_string(builtin, "metal-CuZn-eta")) {
        return BUILTIN_SPECTRUM(Const::CuZn_eta);
    } else if(tokens.is_string(builtin, "metal-CuZn-k")) {
        return BUILTIN_SPECTRUM(Const::CuZn_k);
    } else if(tokens.is_string(builtin, "metal-MgO-eta")) {
        return BUILTIN_SPECTRUM(Const::MgO_eta);
    } else if(tokens.is_string(builtin, "metal-MgO-k")) {
        return BUILTIN_SPECTRUM(Const::MgO_k);
    } else if(tokens.is_string(builtin, "metal-TiO2-eta")) {
        return BUILTIN_SPECTRUM(Const::TiO2_eta);
    } else if(tokens.is_string(builtin, "metal-TiO2-k")) {
        return BUILTIN_SPECTRUM(Const::TiO2_k);
    }

    tokens.fail("Unknown built-in metal.");
}

struct Partial_Scene {

    Partial_Scene() : parser(0) {
    }
    Partial_Scene(Parser parser) : parser(move(parser)) {
    }
    ~Partial_Scene() = default;

    Partial_Scene(const Partial_Scene&) = delete;
    Partial_Scene& operator=(const Partial_Scene&) = delete;

    Partial_Scene(Partial_Scene&&) = default;
    Partial_Scene& operator=(Partial_Scene&&) = default;

    Parser parser;
    Camera camera;

    u64 next_mesh_id_ = 0;
    u64 next_material_id_ = 0;
    u64 next_texture_id_ = 0;
    u64 next_object_id_ = 0;
    u64 next_light_id_ = 0;

    Mesh_ID next_mesh_id() {
        return {parser.scene_depth, next_mesh_id_++};
    }
    Material_ID next_material_id() {
        return {parser.scene_depth, next_material_id_++};
    }
    Texture_ID next_texture_id() {
        return {parser.scene_depth, next_texture_id_++};
    }
    Object_ID next_object_id() {
        return {parser.scene_depth, next_object_id_++};
    }
    Light_ID next_light_id() {
        return {parser.scene_depth, next_light_id_++};
    }

    Map<Mesh_ID, Mesh, Alloc> meshes;
    Map<Object_ID, Object, Alloc> objects;
    Map<Texture_ID, Texture, Alloc> textures;
    Map<Material_ID, Material, Alloc> materials;
    Map<Light_ID, Light, Alloc> lights;

    Vec<Mesh_ID, Alloc> top_level_meshes;
    Vec<Instance, Alloc> top_level_instances;

    Map<Mesh_ID, Async::Task<Mesh>, Alloc> mesh_tasks;
    Map<Texture_ID, Async::Task<Texture>, Alloc> texture_tasks;
    Map<Light_ID, Async::Task<Light>, Alloc> light_tasks;
    Vec<Async::Task<Partial_Scene>, Alloc> import_tasks;

    void add_shape(Opt<Object_ID> object_id, Mesh_ID mesh_id) {
        if(object_id.ok())
            objects.get(*object_id).meshes.push(mesh_id);
        else
            top_level_meshes.push(mesh_id);
    }

    void add_instance(Opt<Object_ID> object_id, Instance instance) {
        if(object_id.ok())
            objects.get(*object_id).instances.push(instance);
        else
            top_level_instances.push(instance);
    }

    Texture_ID add_const_texture(f32 scalar) {
        Texture t;
        t.type = Textures::Type::constant;
        t.data_type = Textures::Data::scalar;
        t.encoding = Textures::Encoding::linear;
        t.scalar = scalar;
        auto id = next_texture_id();
        textures.insert(id, move(t));
        return id;
    }
    Texture_ID add_const_texture(Spectrum rgb) {
        Texture t;
        t.type = Textures::Type::constant;
        t.data_type = Textures::Data::spectrum;
        t.encoding = Textures::Encoding::linear;
        t.spectrum = rgb;
        auto id = next_texture_id();
        textures.insert(id, move(t));
        return id;
    }

    void merge_import(Partial_Scene import) {
        Region(R) {

            Map<u64, u64, Mregion<R>> id_remap_mesh(import.meshes.length());
            Map<u64, u64, Mregion<R>> id_remap_object(import.objects.length());
            Map<u64, u64, Mregion<R>> id_remap_material(import.materials.length());
            Map<u64, u64, Mregion<R>> id_remap_texture(import.textures.length());
            Map<u64, u64, Mregion<R>> id_remap_light(import.lights.length());

            assert(import.mesh_tasks.empty());
            assert(import.texture_tasks.empty());
            assert(import.light_tasks.empty());
            assert(import.import_tasks.empty());

            {
                for(auto& [mesh, _] : import.meshes) {
                    if(mesh.depth > parser.scene_depth) {
                        id_remap_mesh.insert(mesh.id, next_mesh_id_++);
                    }
                }
                for(auto& [obj, _] : import.objects) {
                    if(obj.depth > parser.scene_depth) {
                        id_remap_object.insert(obj.id, next_object_id_++);
                    }
                }
                for(auto& [material, _] : import.materials) {
                    if(material.depth > parser.scene_depth) {
                        id_remap_material.insert(material.id, next_material_id_++);
                    }
                }
                for(auto& [texture, _] : import.textures) {
                    if(texture.depth > parser.scene_depth) {
                        id_remap_texture.insert(texture.id, next_texture_id_++);
                    }
                }
                for(auto& [light, _] : import.lights) {
                    if(light.depth > parser.scene_depth) {
                        id_remap_light.insert(light.id, next_light_id_++);
                    }
                }
            }

            auto remap = [&]<typename T>(const ID<T>& id) -> ID<T> {
                if(!id.invalid() && id.depth > parser.scene_depth) {
                    u64 new_id = UINT64_MAX;
                    if constexpr(Same<T, Mesh>) {
                        new_id = id_remap_mesh.get(id.id);
                    } else if constexpr(Same<T, Object>) {
                        new_id = id_remap_object.get(id.id);
                    } else if constexpr(Same<T, Material>) {
                        new_id = id_remap_material.get(id.id);
                    } else if constexpr(Same<T, Texture>) {
                        new_id = id_remap_texture.get(id.id);
                    } else if constexpr(Same<T, Light>) {
                        new_id = id_remap_light.get(id.id);
                    }
                    assert(new_id != UINT64_MAX);
                    return ID<T>{parser.scene_depth, new_id};
                }
                return id;
            };

            for(auto& [old_id, mesh] : import.meshes) {
                mesh.material = remap(mesh.material);
                auto id = remap(old_id);
                meshes.insert(id, move(mesh));
            }
            for(auto& old_id : import.top_level_meshes) {
                auto id = remap(old_id);
                top_level_meshes.push(id);
            }
            for(auto& instance : import.top_level_instances) {
                instance.object = remap(instance.object);
                top_level_instances.push(instance);
            }

            for(auto& [old_id, object] : import.objects) {
                for(auto& mesh : object.meshes) {
                    mesh = remap(mesh);
                }
                for(auto& instance : object.instances) {
                    instance.object = remap(instance.object);
                }
                auto id = remap(old_id);
                objects.insert(id, move(object));
            }

            for(auto& [old_id, texture] : import.textures) {
                texture.v00 = remap(texture.v00);
                texture.v01 = remap(texture.v01);
                texture.v10 = remap(texture.v10);
                texture.v11 = remap(texture.v11);
                texture.tex1 = remap(texture.tex1);
                texture.tex2 = remap(texture.tex2);
                texture.inside = remap(texture.inside);
                texture.outside = remap(texture.outside);
                texture.amount = remap(texture.amount);
                texture.tex = remap(texture.tex);
                texture.scale = remap(texture.scale);
                auto id = remap(old_id);
                textures.insert(id, move(texture));
            }

            for(auto& [old_id, material] : import.materials) {
                material.roughness = remap(material.roughness);
                material.uroughness = remap(material.uroughness);
                material.vroughness = remap(material.vroughness);
                material.albedo = remap(material.albedo);
                material.g = remap(material.g);
                material.sigma_a = remap(material.sigma_a);
                material.displacement_map = remap(material.displacement_map);
                material.reflectance = remap(material.reflectance);
                material.transmittance = remap(material.transmittance);
                material.eumelanin = remap(material.eumelanin);
                material.pheomelanin = remap(material.pheomelanin);
                material.beta_m = remap(material.beta_m);
                material.beta_n = remap(material.beta_n);
                material.alpha = remap(material.alpha);
                material.eta = remap(material.eta);
                material.k = remap(material.k);
                material.scale = remap(material.scale);
                material.amount = remap(material.amount);
                material.mfp = remap(material.mfp);
                material.sigma_s = remap(material.sigma_s);
                material.conductor_eta = remap(material.conductor_eta);
                material.conductor_k = remap(material.conductor_k);
                material.conductor_roughness = remap(material.conductor_roughness);
                material.conductor_uroughness = remap(material.conductor_uroughness);
                material.conductor_vroughness = remap(material.conductor_vroughness);
                material.interface_eta = remap(material.interface_eta);
                material.interface_k = remap(material.interface_k);
                material.interface_roughness = remap(material.interface_roughness);
                material.interface_uroughness = remap(material.interface_uroughness);
                material.interface_vroughness = remap(material.interface_vroughness);
                material.thickness = remap(material.thickness);
                material.a = remap(material.a);
                material.b = remap(material.b);
                auto id = remap(old_id);
                materials.insert(id, move(material));
            }

            for(auto& [old_id, light] : import.lights) {
                auto id = remap(old_id);
                lights.insert(id, move(light));
            }
        }
    }

    Async::Task<void> resolve() {
        for(auto& [id, task] : texture_tasks) {
            auto texture = co_await task;
            textures.insert(id, move(texture));
        }
        texture_tasks.clear();

        for(auto& [id, task] : light_tasks) {
            auto light = co_await task;
            lights.insert(id, move(light));
        }
        light_tasks.clear();

        for(auto& [id, task] : mesh_tasks) {
            auto mesh = co_await task;
            meshes.insert(id, move(mesh));
        }
        mesh_tasks.clear();

        for(auto& task : import_tasks) {
            auto import = co_await task;
            co_await import.resolve();
            merge_import(move(import));
        }
        import_tasks.clear();
    }

    void set_defaults() {
        auto zero_tex = add_const_texture(0.0f);
        auto point25_tex = add_const_texture(0.25f);
        auto point3_tex = add_const_texture(0.3f);
        auto point5_tex = add_const_texture(0.5f);
        auto point01_tex = add_const_texture(0.01f);
        auto one_tex = add_const_texture(1.0f);
        auto onepoint3_tex = add_const_texture(1.3f);
        auto onepoint33_tex = add_const_texture(1.33f);
        auto onepoint5_tex = add_const_texture(1.5f);
        auto onepoint55_tex = add_const_texture(1.55f);
        auto two_tex = add_const_texture(2.0f);
        auto cu_eta_tex = add_const_texture(BUILTIN_SPECTRUM(Const::Cu_eta));
        auto cu_k_tex = add_const_texture(BUILTIN_SPECTRUM(Const::Cu_k));
        auto sub_sigma_a_tex = add_const_texture(Spectrum{0.0011f, 0.0024f, 0.014f});
        auto sub_sigma_s_tex = add_const_texture(Spectrum{2.55f, 3.21f, 3.77f});

        for(auto& [_, texture] : textures) {
            if(texture.type == Textures::Type::bilerp) {
                if(texture.v00.invalid()) texture.v00 = zero_tex;
                if(texture.v01.invalid()) texture.v00 = one_tex;
                if(texture.v10.invalid()) texture.v00 = zero_tex;
                if(texture.v11.invalid()) texture.v00 = one_tex;
            } else if(texture.type == Textures::Type::checkerboard) {
                if(texture.tex1.invalid()) texture.tex1 = one_tex;
                if(texture.tex2.invalid()) texture.tex2 = zero_tex;
            } else if(texture.type == Textures::Type::directionmix) {
                if(texture.tex1.invalid()) texture.tex1 = zero_tex;
                if(texture.tex2.invalid()) texture.tex2 = one_tex;
            } else if(texture.type == Textures::Type::dots) {
                if(texture.inside.invalid()) texture.inside = one_tex;
                if(texture.outside.invalid()) texture.outside = zero_tex;
            } else if(texture.type == Textures::Type::mix) {
                if(texture.tex1.invalid()) texture.tex1 = zero_tex;
                if(texture.tex2.invalid()) texture.tex2 = one_tex;
                if(texture.amount.invalid()) texture.amount = point5_tex;
            } else if(texture.type == Textures::Type::scale) {
                if(texture.tex.invalid()) texture.tex = one_tex;
                if(texture.scale.invalid()) texture.scale = one_tex;
            }
        }

        for(auto& [_, material] : materials) {
            if(material.type == Materials::Type::conductor ||
               material.type == Materials::Type::dielectric ||
               material.type == Materials::Type::coated_diffuse ||
               material.type == Materials::Type::subsurface) {
                if(material.roughness.invalid()) material.roughness = zero_tex;
                if(material.uroughness.invalid()) material.uroughness = zero_tex;
                if(material.vroughness.invalid()) material.vroughness = zero_tex;
            }

            if(material.type == Materials::Type::coated_diffuse) {

                if(material.albedo.invalid()) material.albedo = zero_tex;
                if(material.g.invalid()) material.g = zero_tex;
                if(material.thickness.invalid()) material.thickness = point01_tex;
                if(material.reflectance.invalid()) material.reflectance = point5_tex;

            } else if(material.type == Materials::Type::coated_conductor) {

                if(material.albedo.invalid()) material.albedo = zero_tex;
                if(material.g.invalid()) material.g = zero_tex;
                if(material.thickness.invalid()) material.thickness = point01_tex;

                if(material.conductor_roughness.invalid()) material.conductor_roughness = zero_tex;
                if(material.conductor_uroughness.invalid())
                    material.conductor_uroughness = zero_tex;
                if(material.conductor_vroughness.invalid())
                    material.conductor_vroughness = zero_tex;
                if(material.interface_roughness.invalid()) material.interface_roughness = zero_tex;
                if(material.interface_uroughness.invalid())
                    material.interface_uroughness = zero_tex;
                if(material.interface_vroughness.invalid())
                    material.interface_vroughness = zero_tex;
                if(material.conductor_eta.invalid()) material.conductor_eta = cu_eta_tex;
                if(material.conductor_k.invalid()) material.conductor_k = cu_k_tex;
                if(material.interface_eta.invalid()) material.interface_eta = cu_eta_tex;
                if(material.interface_k.invalid()) material.interface_k = cu_k_tex;

            } else if(material.type == Materials::Type::conductor) {

                if(material.eta.invalid()) material.eta = cu_eta_tex;
                if(material.k.invalid()) material.k = cu_k_tex;

            } else if(material.type == Materials::Type::dielectric ||
                      material.type == Materials::Type::thin_dielectric) {

                if(material.eta.invalid()) material.eta = onepoint5_tex;

            } else if(material.type == Materials::Type::diffuse) {

                if(material.reflectance.invalid()) material.reflectance = point5_tex;

            } else if(material.type == Materials::Type::diffuse_transmission) {

                if(material.reflectance.invalid()) material.reflectance = point25_tex;
                if(material.transmittance.invalid()) material.transmittance = point25_tex;
                if(material.scale.invalid()) material.scale = one_tex;

            } else if(material.type == Materials::Type::hair) {

                if(!material.sigma_a.invalid()) {
                    material.reflectance = Texture_ID{};
                    material.eumelanin = Texture_ID{};
                    material.pheomelanin = Texture_ID{};
                } else if(!material.reflectance.invalid()) {
                    material.eumelanin = Texture_ID{};
                    material.pheomelanin = Texture_ID{};
                } else {
                    material.eumelanin = onepoint3_tex;
                    material.pheomelanin = zero_tex;
                }
                if(material.eta.invalid()) material.eta = onepoint55_tex;
                if(material.beta_m.invalid()) material.beta_m = point3_tex;
                if(material.beta_n.invalid()) material.beta_n = point3_tex;

                if(material.alpha.invalid()) material.alpha = two_tex;
            } else if(material.type == Materials::Type::mix) {

                if(material.amount.invalid()) material.amount = point5_tex;

            } else if(material.type == Materials::Type::subsurface) {

                if(material.eta.invalid()) material.eta = onepoint33_tex;
                if(material.g.invalid()) material.g = zero_tex;
                if(material.sigma_a.invalid()) material.sigma_a = sub_sigma_a_tex;
                if(material.sigma_s.invalid()) material.sigma_s = sub_sigma_s_tex;
                if(material.scale.invalid()) material.scale = one_tex;
            }
        }
    }

    Async::Task<Scene> to_scene() {
        co_await resolve();

        set_defaults();

        assert(texture_tasks.empty());
        assert(light_tasks.empty());
        assert(mesh_tasks.empty());
        assert(import_tasks.empty());

        Scene ret;
        ret.camera = camera;

        ret.top_level_meshes = move(top_level_meshes);
        ret.top_level_instances = move(top_level_instances);
        ret.objects.resize(next_object_id_);
        ret.meshes.resize(next_mesh_id_);
        ret.materials.resize(next_material_id_);
        ret.textures.resize(next_texture_id_);
        ret.lights.resize(next_light_id_);

        for(auto& [id, object] : objects) {
            assert(id.depth == 0);
            ret.objects[id.id] = move(object);
        }
        for(auto& [id, mesh] : meshes) {
            assert(id.depth == 0);
            ret.meshes[id.id] = move(mesh);
        }
        for(auto& [id, material] : materials) {
            assert(id.depth == 0);
            ret.materials[id.id] = move(material);
        }
        for(auto& [id, texture] : textures) {
            assert(id.depth == 0);
            ret.textures[id.id] = move(texture);
        }
        for(auto& [id, light] : lights) {
            assert(id.depth == 0);
            ret.lights[id.id] = move(light);
        }

        co_return ret;
    }
};

static void ignore_list(Tokenizer& tokens) {
    tokens.expect_lbracket();
    while(true) {
        if(tokens.is_rbracket(tokens.peek())) break;
        tokens.skip();
    }
    tokens.expect_rbracket();
}

static void ignore_quoted(Tokenizer& tokens) {
    tokens.expect_quoted_string();
}

static void ignore_list_of_quoted(Tokenizer& tokens) {
    tokens.expect_lbracket();
    while(true) {
        if(tokens.is_rbracket(tokens.peek())) break;
        tokens.expect_quoted_string();
    }
    tokens.expect_rbracket();
}

static void ignore_parameter(Tokenizer& tokens, auto type) {
    if(tokens.type_is_string(type) || tokens.type_is_texture(type)) {
        if(tokens.is_quote(tokens.peek())) {
            ignore_quoted(tokens);
        } else {
            ignore_list_of_quoted(tokens);
        }
    } else if(tokens.type_is_float_array(type)) {
        if(tokens.is_float(tokens.peek())) {
            tokens.expect_float();
        } else {
            ignore_list(tokens);
        }
    } else if(tokens.type_is_int(type)) {
        if(tokens.is_int(tokens.peek())) {
            tokens.expect_int();
        } else {
            ignore_list(tokens);
        }
    } else if(tokens.type_is_bool(type)) {
        if(tokens.is_string(tokens.peek(), "true") || tokens.is_string(tokens.peek(), "false")) {
            tokens.skip();
        } else {
            ignore_list(tokens);
        }
    } else if(tokens.type_is_spectrum(type) || tokens.type_is_rgb(type)) {
        if(tokens.is_float(tokens.peek())) {
            tokens.expect_float();
        } else if(tokens.is_quote(tokens.peek())) {
            ignore_quoted(tokens);
        } else {
            ignore_list(tokens);
        }
    } else if(tokens.type_is_blackbody(type)) {
        if(tokens.is_int(tokens.peek())) {
            tokens.expect_int();
        } else {
            ignore_list(tokens);
        }
    } else {
        tokens.fail("Unknown type.");
    }
}

static void ignore_attributes(Tokenizer& tokens) {
    while(true) {
        if(!tokens.is_quote(tokens.peek())) break;
        tokens.expect_quote();
        auto type = tokens.next();
        tokens.skip();
        tokens.expect_quote();
        ignore_parameter(tokens, type);
    }
}

static void ignore_defn(Tokenizer& tokens, u64 modifiers = 1) {
    for(u64 i = 0; i < modifiers; i++) {
        ignore_quoted(tokens);
    }
    ignore_attributes(tokens);
}

static Vec<u32, Alloc> parse_int_list(Tokenizer& tokens) {
    Vec<u32, Alloc> list;
    tokens.expect_lbracket();
    while(true) {
        if(tokens.is_rbracket(tokens.peek())) break;
        list.push(tokens.expect_int());
    }
    tokens.expect_rbracket();
    return list;
}

static Vec<f32, Alloc> parse_float_list(Tokenizer& tokens) {
    Vec<f32, Alloc> list;
    tokens.expect_lbracket();
    while(true) {
        if(tokens.is_rbracket(tokens.peek())) break;
        list.push(tokens.expect_float());
    }
    tokens.expect_rbracket();
    return list;
}

static i32 parse_int_or_int_list(Tokenizer& tokens) {
    auto token = tokens.peek();
    if(tokens.is_lbracket(token)) {
        tokens.expect_lbracket();
        i32 value = tokens.expect_int();
        tokens.expect_rbracket();
        return value;
    }
    return tokens.expect_int();
}

static Vec3 parse_bracketed_vec3(Tokenizer& tokens) {
    tokens.expect_lbracket();
    f32 x = tokens.expect_float();
    f32 y = tokens.expect_float();
    f32 z = tokens.expect_float();
    tokens.expect_rbracket();
    return Vec3{x, y, z};
}

static f32 parse_float_or_float_list(Tokenizer& tokens) {
    auto token = tokens.peek();
    if(tokens.is_lbracket(token)) {
        tokens.expect_lbracket();
        f32 value = tokens.expect_float();
        tokens.expect_rbracket();
        return value;
    }
    return tokens.expect_float();
}

static bool parse_bool_or_bool_list(Tokenizer& tokens) {
    auto token = tokens.peek();
    if(tokens.is_lbracket(token)) {
        tokens.expect_lbracket();
        bool value = tokens.expect_bool();
        tokens.expect_rbracket();
        return value;
    }
    return tokens.expect_bool();
}

static Tokenizer::Token parse_string_or_string_list(Tokenizer& tokens) {
    Tokenizer::Token str;
    auto token = tokens.peek();
    if(tokens.is_quote(token)) {
        str = tokens.expect_quoted_string();
    } else if(tokens.is_lbracket(token)) {
        tokens.expect_lbracket();
        str = tokens.expect_quoted_string();
        tokens.expect_rbracket();
    } else {
        tokens.fail("Expected string or string list.");
    }
    return str;
}

static void ignore_type(Tokenizer& tokens, auto type) {
    if(tokens.type_is_string(type)) {
        parse_string_or_string_list(tokens);
    } else {
        tokens.skip();
    }
}

static Mat4 parse_transform(Tokenizer& tokens) {
    Mat4 transform;
    tokens.expect_lbracket();
    for(u32 i = 0; i < 16; i++) {
        f32 f = tokens.expect_float();
        transform.data[i] = f;
    }
    tokens.expect_rbracket();
    return transform;
}

static Vec3 parse_vec3(Tokenizer& tokens) {
    Vec3 vec;
    vec.x = tokens.expect_float();
    vec.y = tokens.expect_float();
    vec.z = tokens.expect_float();
    return vec;
}

static void transform_normals(Vec<f32, Alloc>& normals, Mat4 transform) {
    assert(normals.length() % 3 == 0);
    Mat4 T = transform.inverse().T();
    for(u64 i = 0; i < normals.length(); i += 3) {
        Vec3 n{normals[i], normals[i + 1], normals[i + 2]};
        n = Math::normalize(T.rotate(n));
        normals[i] = n.x;
        normals[i + 1] = n.y;
        normals[i + 2] = n.z;
    }
}

static Async::Task<Mesh> load_ply_async(Async::Pool<>& pool, Parser::Graphics state,
                                        String<Alloc> directory, String<Alloc> filename,
                                        Material_ID material, Texture_ID alpha) {

    co_await pool.suspend();
    Mesh mesh = RPLY::load(directory.view(), filename.view());
    mesh.alpha = alpha;
    mesh.material = material;
    mesh.mesh_to_instance = state.transform;
    if(state.area_light.L != Vec3{0.0f}) {
        mesh.emission = state.area_light.L * state.area_light.scale;
    }
    transform_normals(mesh.normals, mesh.mesh_to_instance);
    if(state.reverse_orientation) {
        mesh.reverse_orientation();
    }
    co_return mesh;
}

static Opt<Variant<Image_Data<u8>, Image_Data<f32>>> parse_image_data(String_View filename,
                                                                      Slice<const u8> file) {

    if(filename.file_extension() == "pfm"_v) {

        if(file.length() < 7 || !(file[0] == 'P' && (file[1] == 'F' || file[1] == 'f') &&
                                  ascii::is_whitespace(file[2]))) {
            warn("[PBRT] invalid PFM image %.", filename);
            return {};
        }

        i32 w = 0, h = 0;
        f32 scale = 1.0f;
        bool little_endian = false;
        i32 channels = file[1] == 'F' ? 3 : 1;

        String_View next{&file[3], file.length() - 3};
        if(auto w_ = Format::parse_i64(next); w_.ok()) {
            w = static_cast<i32>(w_->first);
            next = w_->second;
        } else {
            warn("[PBRT] invalid PFM image %.", filename);
            return {};
        }
        if(auto h_ = Format::parse_i64(next); h_.ok()) {
            h = static_cast<i32>(h_->first);
            next = h_->second;
        } else {
            warn("[PBRT] invalid PFM image %.", filename);
            return {};
        }
        if(auto s_ = Format::parse_f32(next); s_.ok()) {
            scale = s_->first;
            next = s_->second;
            little_endian = scale < 0.0f;
            scale = Math::abs(scale);
        } else {
            warn("[PBRT] invalid PFM image %.", filename);
            return {};
        }

        auto data = Vec<f32, Alloc>::make(w * h * channels);

        next = Format::skip_whitespace(next);

        if(next.length() < static_cast<u64>(w * h * channels * 4)) {
            warn("[PBRT] invalid PFM image %.", filename);
            return {};
        }

        Libc::memcpy(data.data(), next.data(), data.length() * sizeof(f32));

        if(!little_endian) {
            u8 bytes[4];
            for(u32 i = 0; i < data.length(); i++) {
                Libc::memcpy(bytes, &data[i], 4);
                swap(bytes[0], bytes[3]);
                swap(bytes[1], bytes[2]);
                Libc::memcpy(&data[i], bytes, 4);
            }
        }

        return Opt{Variant<Image_Data<u8>, Image_Data<f32>>{Image_Data<f32>{
            .data = move(data),
            .w = static_cast<u32>(w),
            .h = static_cast<u32>(h),
            .channels = static_cast<u32>(channels),
        }}};

    } else if(IsEXRFromMemory(file.data(), file.length()) == TINYEXR_SUCCESS) {

        const char* err_str = null;
        f32* data = null;
        i32 w = 0, h = 0;

        if(LoadEXRFromMemory(&data, &w, &h, file.data(), file.length(), &err_str) < 0) {
            warn("[PBRT] failed to load EXR image from %: %.", filename, String_View{err_str});
            FreeEXRErrorMessage(err_str);
            return {};
        }

        auto vec = Vec<f32, Alloc>::make(w * h * 4);
        Libc::memcpy(vec.data(), data, vec.length() * sizeof(f32));
        Libc::free(data);

        return Opt{Variant<Image_Data<u8>, Image_Data<f32>>{Image_Data<f32>{
            .data = move(vec),
            .w = static_cast<u32>(w),
            .h = static_cast<u32>(h),
            .channels = 4,
        }}};

    } else {

        i32 w = 0, h = 0, channels = 0;
        u8* data = stbi_load_from_memory(file.data(), static_cast<int>(file.length()), &w, &h,
                                         &channels, 0);

        if(!data) {
            warn("[PBRT] failed to load image from %: %.", filename,
                 String_View{stbi_failure_reason()});
            return {};
        }

        auto vec = Vec<u8, Alloc>::make(w * h * channels);
        Libc::memcpy(vec.data(), data, vec.length());
        stbi_image_free(data);

        return Opt{Variant<Image_Data<u8>, Image_Data<f32>>{Image_Data<u8>{
            .data = move(vec),
            .w = static_cast<u32>(w),
            .h = static_cast<u32>(h),
            .channels = static_cast<u32>(channels),
        }}};
    }
}

static Async::Task<Light> complete_light_async(Async::Pool<>& pool, String<Alloc> directory,
                                               String<Alloc> filename, Light light) {

#if LOAD_TEXTURES == 1
    auto path = format<Alloc>("%/%\x00"_v, directory, filename);

    auto file_ = co_await Async::read(pool, path.view());
    if(!file_.ok()) co_return light;
    auto file = move(*file_);

    co_await pool.suspend();

    if(auto data = parse_image_data(filename.view(), file.slice()); data.ok()) {
        move(*data).match(Overload{
            [&](Image_Data<u8>&&) { warn("[PBRT] ignoring non-HDR environment map image."); },
            [&](Image_Data<f32>&& data) { light.map = move(data); },
        });
    }

    if(light.map.channels == 1 || light.map.channels == 2) {
        f32 scale = light.scale.x * 0.2126f + light.scale.y * 0.7152f + light.scale.z * 0.0722f;
        for(f32& f : light.map.data) f *= scale;
    } else if(light.map.channels == 3) {
        for(u32 i = 0; i < light.map.data.length(); i += 3) {
            Vec3* pixel = reinterpret_cast<Vec3*>(&light.map.data[i]);
            *pixel = *pixel * light.scale;
        }
    } else if(light.map.channels == 4) {
        for(u32 i = 0; i < light.map.data.length(); i += 4) {
            Vec3* pixel = reinterpret_cast<Vec3*>(&light.map.data[i]);
            *pixel = *pixel * light.scale;
        }
    } else {
        warn("[PBRT] found environment map with % channels.", light.map.channels);
    }
#endif

    co_return light;
}

static Async::Task<Texture> complete_texture_async(Async::Pool<>& pool, String<Alloc> directory,
                                                   String<Alloc> filename, Texture texture) {

#if LOAD_TEXTURES == 1
    auto path = format<Alloc>("%/%\x00"_v, directory, filename);

    if(texture.type == Textures::Type::ptex) {
        warn("[PBRT] ignoring ptex texture.");
        co_return texture;
    }

    auto file_ = co_await Async::read(pool, path.view());
    if(!file_.ok()) co_return texture;
    auto file = move(*file_);

    co_await pool.suspend();

    if(auto data = parse_image_data(filename.view(), file.slice()); data.ok()) {
        texture.image = move(*data);
    }

#endif

    co_return texture;
}

static Textures::Data texture_data_for(Tokenizer& tokens, Tokenizer::Token kind) {

    if(tokens.is_string(kind, "float")) {
        return Textures::Data::scalar;
    } else if(tokens.is_string(kind, "spectrum")) {
        return Textures::Data::spectrum;
    }

    tokens.fail("Unknown texture data type.");
}

static Textures::Type texture_type_for(Tokenizer& tokens, Tokenizer::Token kind) {

    if(tokens.is_string(kind, "bilerp")) {
        return Textures::Type::bilerp;
    } else if(tokens.is_string(kind, "checkerboard")) {
        return Textures::Type::checkerboard;
    } else if(tokens.is_string(kind, "constant")) {
        return Textures::Type::constant;
    } else if(tokens.is_string(kind, "directionmix")) {
        return Textures::Type::directionmix;
    } else if(tokens.is_string(kind, "dots")) {
        return Textures::Type::dots;
    } else if(tokens.is_string(kind, "fbm")) {
        return Textures::Type::fbm;
    } else if(tokens.is_string(kind, "imagemap")) {
        return Textures::Type::imagemap;
    } else if(tokens.is_string(kind, "marble")) {
        return Textures::Type::marble;
    } else if(tokens.is_string(kind, "mix")) {
        return Textures::Type::mix;
    } else if(tokens.is_string(kind, "ptex")) {
        return Textures::Type::ptex;
    } else if(tokens.is_string(kind, "scale")) {
        return Textures::Type::scale;
    } else if(tokens.is_string(kind, "windy")) {
        return Textures::Type::windy;
    } else if(tokens.is_string(kind, "wrinkled")) {
        return Textures::Type::wrinkled;
    }

    tokens.fail("Unknown texture type.");
}

static Materials::Type material_type_for(Tokenizer& tokens, Tokenizer::Token kind) {

    if(tokens.is_string(kind, "conductor")) {
        return Materials::Type::conductor;
    } else if(tokens.is_string(kind, "dielectric")) {
        return Materials::Type::dielectric;
    } else if(tokens.is_string(kind, "diffuse")) {
        return Materials::Type::diffuse;
    } else if(tokens.is_string(kind, "diffusetransmission")) {
        return Materials::Type::diffuse_transmission;
    } else if(tokens.is_string(kind, "mix")) {
        return Materials::Type::mix;
    } else if(tokens.is_string(kind, "coateddiffuse")) {
        return Materials::Type::coated_diffuse;
    } else if(tokens.is_string(kind, "coatedconductor")) {
        return Materials::Type::coated_conductor;
    } else if(tokens.is_string(kind, "hair")) {
        return Materials::Type::hair;
    } else if(tokens.is_string(kind, "interface")) {
        return Materials::Type::interface;
    } else if(tokens.is_string(kind, "measured")) {
        return Materials::Type::measured;
    } else if(tokens.is_string(kind, "subsurface")) {
        return Materials::Type::subsurface;
    } else if(tokens.is_string(kind, "thindielectric")) {
        return Materials::Type::thin_dielectric;
    } else if(tokens.is_string(kind, "")) {
        return Materials::Type::diffuse;
    }

    tokens.fail("Unknown material type.");
}

template<typename F>
static bool parse_texture_texture_reference(Tokenizer& tokens, Tokenizer::Token name,
                                            Texture& texture, F&& make_id) {
    if(tokens.is_string(name, "v00")) {
        texture.v00 = make_id();
        return true;
    } else if(tokens.is_string(name, "v01")) {
        texture.v01 = make_id();
        return true;
    } else if(tokens.is_string(name, "v10")) {
        texture.v10 = make_id();
        return true;
    } else if(tokens.is_string(name, "v11")) {
        texture.v11 = make_id();
        return true;
    } else if(tokens.is_string(name, "tex1")) {
        texture.tex1 = make_id();
        return true;
    } else if(tokens.is_string(name, "tex2")) {
        texture.tex2 = make_id();
        return true;
    } else if(tokens.is_string(name, "inside")) {
        texture.inside = make_id();
        return true;
    } else if(tokens.is_string(name, "outside")) {
        texture.outside = make_id();
        return true;
    } else if(tokens.is_string(name, "amount")) {
        texture.amount = make_id();
        return true;
    } else if(tokens.is_string(name, "tex")) {
        texture.tex = make_id();
        return true;
    } else if(tokens.is_string(name, "scale")) {
        texture.scale = make_id();
        return true;
    }
    return false;
}

template<typename F>
static bool parse_material_texture_reference(Tokenizer& tokens, Tokenizer::Token name,
                                             Material& material, F&& make_id) {
    if(tokens.is_string(name, "displacement")) {
        material.displacement_map = make_id();
        return true;
    } else if(tokens.is_string(name, "roughness")) {
        material.roughness = make_id();
        return true;
    } else if(tokens.is_string(name, "interface.roughness")) {
        material.interface_roughness = make_id();
        return true;
    } else if(tokens.is_string(name, "interface.uroughness")) {
        material.interface_uroughness = make_id();
        return true;
    } else if(tokens.is_string(name, "interface.vroughness")) {
        material.interface_vroughness = make_id();
        return true;
    } else if(tokens.is_string(name, "interface.eta")) {
        material.interface_eta = make_id();
        return true;
    } else if(tokens.is_string(name, "interface.k")) {
        material.interface_k = make_id();
        return true;
    } else if(tokens.is_string(name, "conductor.roughness")) {
        material.conductor_roughness = make_id();
        return true;
    } else if(tokens.is_string(name, "conductor.uroughness")) {
        material.conductor_uroughness = make_id();
        return true;
    } else if(tokens.is_string(name, "conductor.vroughness")) {
        material.conductor_vroughness = make_id();
        return true;
    } else if(tokens.is_string(name, "conductor.eta")) {
        material.conductor_eta = make_id();
        return true;
    } else if(tokens.is_string(name, "conductor.k")) {
        material.conductor_k = make_id();
        return true;
    } else if(tokens.is_string(name, "uroughness")) {
        material.uroughness = make_id();
        return true;
    } else if(tokens.is_string(name, "vroughness")) {
        material.vroughness = make_id();
        return true;
    } else if(tokens.is_string(name, "albedo")) {
        material.albedo = make_id();
        return true;
    } else if(tokens.is_string(name, "g")) {
        material.g = make_id();
        return true;
    } else if(tokens.is_string(name, "reflectance")) {
        material.reflectance = make_id();
        return true;
    } else if(tokens.is_string(name, "eta")) {
        material.eta = make_id();
        return true;
    } else if(tokens.is_string(name, "k")) {
        material.k = make_id();
        return true;
    } else if(tokens.is_string(name, "transmittance")) {
        material.transmittance = make_id();
        return true;
    } else if(tokens.is_string(name, "sigma_a")) {
        material.sigma_a = make_id();
        return true;
    } else if(tokens.is_string(name, "eumelanin")) {
        material.eumelanin = make_id();
        return true;
    } else if(tokens.is_string(name, "pheomelanin")) {
        material.pheomelanin = make_id();
        return true;
    } else if(tokens.is_string(name, "beta_m")) {
        material.beta_m = make_id();
        return true;
    } else if(tokens.is_string(name, "beta_n")) {
        material.beta_n = make_id();
        return true;
    } else if(tokens.is_string(name, "alpha")) {
        material.alpha = make_id();
        return true;
    } else if(tokens.is_string(name, "amount")) {
        material.amount = make_id();
        return true;
    } else if(tokens.is_string(name, "mfp")) {
        material.mfp = make_id();
        return true;
    } else if(tokens.is_string(name, "sigma_s")) {
        material.sigma_s = make_id();
        return true;
    } else if(tokens.is_string(name, "thickness")) {
        material.thickness = make_id();
        return true;
    } else if(tokens.is_string(name, "scale")) {
        material.scale = make_id();
        return true;
    }
    return false;
}

static void parse_material_attributes(Partial_Scene& scene, Tokenizer& tokens, Material& material) {

    Parser& parser = scene.parser;

    while(true) {
        if(!tokens.is_quote(tokens.peek())) break;
        tokens.expect_quote();
        auto type = tokens.next();
        auto name = tokens.next();
        tokens.expect_quote();

        if(tokens.type_is_string(type)) {

            if(tokens.is_string(name, "materials")) {
                tokens.expect_lbracket();
                auto a = tokens.expect_quoted_string();
                auto b = tokens.expect_quoted_string();
                tokens.expect_rbracket();
                material.a = parser.named_material(tokens.to_string(a));
                material.b = parser.named_material(tokens.to_string(b));
            } else {
                auto value = parse_string_or_string_list(tokens);
                if(tokens.is_string(name, "type")) {
                    material.type = material_type_for(tokens, value);
                } else if(tokens.is_string(name, "normalmap")) {
                    material.normal_map = tokens.to_string(value).string<Alloc>();
                } else if(tokens.is_string(name, "filename")) {
                    material.measured = tokens.to_string(value).string<Alloc>();
                } else if(tokens.is_string(name, "name")) {
                    material.sss_coefficients = tokens.to_string(value).string<Alloc>();
                } else {
                    tokens.fail("Unknown material attribute.");
                }
            }

        } else if(tokens.type_is_texture(type)) {

            auto value = parse_string_or_string_list(tokens);
            if(parse_material_texture_reference(tokens, name, material, [&]() {
                   return parser.named_texture(tokens.to_string(value));
               })) {
            } else {
                auto name_str = tokens.to_string(name);
                if(name_str.length() && name_str[name_str.length() - 1] == '1') {
                    material.a = parser.named_material(tokens.to_string(value));
                } else if(name_str.length() && name_str[name_str.length() - 1] == '2') {
                    material.b = parser.named_material(tokens.to_string(value));
                } else {
                    tokens.fail("Unknown material attribute.");
                }
            }

        } else if(tokens.type_is_float(type)) {

            f32 value = parse_float_or_float_list(tokens);
            if(!parse_material_texture_reference(
                   tokens, name, material, [&]() { return scene.add_const_texture(value); })) {
                tokens.fail("Unknown material attribute.");
            }

        } else if(tokens.type_is_int(type)) {

            i32 value = parse_int_or_int_list(tokens);
            if(tokens.is_string(name, "maxdepth")) {
                material.max_depth = value;
            } else if(tokens.is_string(name, "nsamples")) {
                material.n_samples = value;
            } else {
                tokens.fail("Unknown material attribute.");
            }

        } else if(tokens.type_is_bool(type)) {

            bool value = parse_bool_or_bool_list(tokens);
            if(tokens.is_string(name, "remaproughness")) {
                material.remap_roughness = value;
            } else {
                tokens.fail("Unknown material attribute.");
            }

        } else if(tokens.type_is_spectrum(type)) {

            bool bracketed = false;
            if(tokens.is_lbracket(tokens.peek())) {
                bracketed = true;
                tokens.expect_lbracket();
            }

            auto next = tokens.peek();
            if(tokens.is_float(next)) {
                Region(R) {
                    Vec<f64, Mregion<R>> list;
                    while(true) {
                        if(tokens.is_rbracket(tokens.peek())) break;
                        list.push(static_cast<f64>(tokens.expect_float()));
                    }
                    if(list.length() < 4 || list.length() % 2 != 0)
                        tokens.fail("Invalid spectrum.");
                    if(!parse_material_texture_reference(tokens, name, material, [&]() {
                           return scene.add_const_texture(averaged_spectrum(list.slice()));
                       })) {
                        tokens.fail("Unknown material attribute.");
                    }
                }
            } else if(tokens.is_quote(next)) {
                auto builtin = tokens.expect_quoted_string();
                if(!parse_material_texture_reference(tokens, name, material, [&]() {
                       return scene.add_const_texture(builtin_constant(tokens, builtin));
                   })) {
                    tokens.fail("Unknown material attribute.");
                }
            } else {
                tokens.fail("Unknown material attribute.");
            }

            if(bracketed) {
                tokens.expect_rbracket();
            }

        } else if(tokens.type_is_rgb(type)) {

            Spectrum value = parse_bracketed_vec3(tokens);
            if(tokens.is_string(name, "color")) {
                material.color = value;
            } else if(!parse_material_texture_reference(tokens, name, material, [&]() {
                          return scene.add_const_texture(value);
                      })) {
                tokens.fail("Unknown material attribute.");
            }

        } else {
            tokens.fail("Unknown material attribute type.");
        }
    }
}

static Material_ID parse_partial_scene_self_material(Tokenizer& tokens, Partial_Scene& scene) {
    Material m;
    auto kind = tokens.expect_quoted_string();
    m.type = material_type_for(tokens, kind);
    parse_material_attributes(scene, tokens, m);
    auto id = scene.next_material_id();
    scene.materials.insert(id, move(m));
    return id;
}

static void parse_partial_scene_self_material_named(Tokenizer& tokens, Partial_Scene& scene) {
    Parser& parser = scene.parser;
    auto name = tokens.expect_quoted_string();
    Material m;
    parse_material_attributes(scene, tokens, m);
    auto id = scene.next_material_id();
    scene.materials.insert(id, move(m));
    parser.named_material(tokens.to_string(name), id);
}

static Light_ID parse_partial_scene_self_light(Async::Pool<>& pool, Tokenizer& tokens,
                                               Partial_Scene& scene) {

    Parser& parser = scene.parser;

    String<Alloc> filename;
    Light light;

    auto kind = tokens.expect_quoted_string();
    if(tokens.is_string(kind, "distant")) {
        light.type = Lights::Type::distant;
    } else if(tokens.is_string(kind, "goniometric")) {
        light.type = Lights::Type::goniometric;
    } else if(tokens.is_string(kind, "infinite")) {
        light.type = Lights::Type::infinite;
    } else if(tokens.is_string(kind, "point")) {
        light.type = Lights::Type::point;
    } else if(tokens.is_string(kind, "projection")) {
        light.type = Lights::Type::projection;
    } else if(tokens.is_string(kind, "spot")) {
        light.type = Lights::Type::spot;
    } else {
        tokens.fail("Unknown light type.");
    }

    while(true) {
        if(!tokens.is_quote(tokens.peek())) break;
        tokens.expect_quote();
        auto type = tokens.next();
        auto name = tokens.next();
        tokens.expect_quote();

        if(tokens.type_is_string(type)) {
            if(tokens.is_string(name, "filename") || tokens.is_string(name, "mapname")) {
                filename = tokens.to_string(parse_string_or_string_list(tokens)).string<Alloc>();
            } else {
                tokens.fail("Unknown light attribute.");
            }
        } else if(tokens.type_is_float(type)) {
            if(tokens.is_string(name, "power")) {
                light.power = parse_float_or_float_list(tokens);
            } else if(tokens.is_string(name, "illuminance")) {
                light.illuminance = parse_float_or_float_list(tokens);
            } else if(tokens.is_string(name, "scale")) {
                light.scale = Spectrum{parse_float_or_float_list(tokens)};
            } else if(tokens.is_string(name, "fov")) {
                light.fov = parse_float_or_float_list(tokens);
            } else if(tokens.is_string(name, "coneangle")) {
                light.cone_angle = parse_float_or_float_list(tokens);
            } else if(tokens.is_string(name, "conedeltaangle")) {
                light.cone_delta_angle = parse_float_or_float_list(tokens);
            } else {
                tokens.fail("Unknown light attribute.");
            }
        } else if(tokens.type_is_spectrum(type) || tokens.type_is_rgb(type)) {
            if(tokens.is_string(name, "L")) {
                light.L = parse_bracketed_vec3(tokens);
            } else if(tokens.is_string(name, "I")) {
                light.I = parse_bracketed_vec3(tokens);
            } else if(tokens.is_string(name, "scale")) {
                light.scale = parse_bracketed_vec3(tokens);
            } else {
                tokens.fail("Unknown light attribute.");
            }
        } else if(tokens.type_is_blackbody(type)) {
            f32 temp = parse_float_or_float_list(tokens);
            if(tokens.is_string(name, "L")) {
                light.L = builtin_blackbody(temp);
            } else if(tokens.is_string(name, "I")) {
                light.I = builtin_blackbody(temp);
            } else {
                tokens.fail("Unknown light attribute.");
            }
        } else if(tokens.type_is_point(type)) {
            if(tokens.is_string(name, "from")) {
                light.from = parse_bracketed_vec3(tokens);
            } else if(tokens.is_string(name, "to")) {
                light.to = parse_bracketed_vec3(tokens);
            } else if(tokens.is_string(name, "portal")) {
                auto portal = parse_float_list(tokens);
                if(portal.length() == 12) {
                    light.portal[0] = Vec3{portal[0], portal[1], portal[2]};
                    light.portal[1] = Vec3{portal[3], portal[4], portal[5]};
                    light.portal[2] = Vec3{portal[6], portal[7], portal[8]};
                    light.portal[3] = Vec3{portal[9], portal[10], portal[11]};
                } else {
                    tokens.fail("Invalid portal.");
                }
            } else {
                tokens.fail("Unknown light attribute.");
            }
        }
    }

    auto id = scene.next_light_id();
    if(filename.empty()) {

        if(light.type == Lights::Type::infinite) {
            if(light.map.data.empty()) {
                light.map.data.push(light.L.x * light.scale.x);
                light.map.data.push(light.L.y * light.scale.y);
                light.map.data.push(light.L.z * light.scale.z);
                light.map.data.push(1.0f);
                light.map.w = 1;
                light.map.h = 1;
                light.map.channels = 4;
            }
        }

        scene.lights.insert(id, move(light));
    } else {
        scene.light_tasks.insert(
            id, complete_light_async(pool, parser.directory.clone(), move(filename), move(light)));
    }

    return id;
}

static void parse_partial_scene_self_texture(Async::Pool<>& pool, Tokenizer& tokens,
                                             Partial_Scene& scene) {
    Parser& parser = scene.parser;

    auto tex_name = tokens.expect_quoted_string();
    auto tex_type = tokens.expect_quoted_string();
    auto tex_kind = tokens.expect_quoted_string();

    Texture texture;
    String<Alloc> filename;

    texture.type = texture_type_for(tokens, tex_kind);
    texture.data_type = texture_data_for(tokens, tex_type);

    if(texture.type == Textures::Type::ptex) {
        texture.encoding = Textures::Encoding::gamma;
    }

    while(true) {
        if(!tokens.is_quote(tokens.peek())) break;
        tokens.expect_quote();
        auto type = tokens.next();
        auto name = tokens.next();
        tokens.expect_quote();

        if(tokens.type_is_string(type)) {

            auto value = parse_string_or_string_list(tokens);
            if(tokens.is_string(name, "mapping")) {
                if(tokens.is_string(value, "uv")) {
                    texture.map = Textures::Map::uv;
                } else if(tokens.is_string(value, "spherical")) {
                    texture.map = Textures::Map::spherical;
                } else if(tokens.is_string(value, "cylindrical")) {
                    texture.map = Textures::Map::cylindrical;
                } else if(tokens.is_string(value, "planar")) {
                    texture.map = Textures::Map::planar;
                } else if(tokens.is_string(value, "spherical")) {
                    texture.map = Textures::Map::spherical;
                } else {
                    tokens.fail("Unknown texture mapping.");
                }
            } else if(tokens.is_string(name, "wrap")) {
                if(tokens.is_string(value, "repeat")) {
                    texture.wrap = Textures::Wrap::repeat;
                } else if(tokens.is_string(value, "black")) {
                    texture.wrap = Textures::Wrap::black;
                } else if(tokens.is_string(value, "clamp")) {
                    texture.wrap = Textures::Wrap::clamp;
                } else {
                    tokens.fail("Unknown texture mapping.");
                }
            } else if(tokens.is_string(name, "filter")) {
                if(tokens.is_string(value, "point")) {
                    texture.filter = Textures::Filter::point;
                } else if(tokens.is_string(value, "bilinear")) {
                    texture.filter = Textures::Filter::bilinear;
                } else if(tokens.is_string(value, "trilinear")) {
                    texture.filter = Textures::Filter::trilinear;
                } else if(tokens.is_string(value, "ewa")) {
                    texture.filter = Textures::Filter::ewa;
                } else {
                    tokens.fail("Unknown texture mapping.");
                }
            } else if(tokens.is_string(name, "encoding")) {
                if(tokens.is_string(value, "sRGB")) {
                    texture.encoding = Textures::Encoding::sRGB;
                } else if(tokens.is_string(value, "linear")) {
                    texture.encoding = Textures::Encoding::linear;
                } else {
                    auto sv = tokens.to_string(value);
                    if(auto split = Format::parse_string(sv); split.ok()) {
                        auto [fst, snd] = move(*split);
                        if(fst == "gamma"_v) {
                            texture.encoding = Textures::Encoding::gamma;
                            if(auto g = Format::parse_f32(snd); g.ok()) {
                                texture.gamma = g->first;
                            } else {
                                tokens.fail("Could not parse gamma value.");
                            }
                        } else {
                            tokens.fail("Unknown texture mapping.");
                        }
                    } else {
                        tokens.fail("Unknown texture mapping.");
                    }
                }
            } else if(tokens.is_string(name, "filename")) {
                filename = tokens.to_string(value).string<Alloc>();
            } else {
                tokens.fail("Unknown texture attribute.");
            }

        } else if(tokens.type_is_float(type)) {

            f32 value = parse_float_or_float_list(tokens);
            if(tokens.is_string(name, "uscale")) {
                texture.u_scale = value;
            } else if(tokens.is_string(name, "vscale")) {
                texture.v_scale = value;
            } else if(tokens.is_string(name, "udelta")) {
                texture.u_delta = value;
            } else if(tokens.is_string(name, "vdelta")) {
                texture.v_delta = value;
            } else if(tokens.is_string(name, "roughness")) {
                texture.roughness = value;
            } else if(tokens.is_string(name, "variation")) {
                texture.variation = value;
            } else if(tokens.is_string(name, "maxanisotropy")) {
                texture.max_anisotropy = value;
            } else if(tokens.is_string(name, "value")) {
                texture.scalar = value;
            } else if(!parse_texture_texture_reference(tokens, name, texture, [&]() {
                          return scene.add_const_texture(value);
                      })) {
                tokens.fail("Unknown texture attribute.");
            }

        } else if(tokens.type_is_vec3(type)) {

            Vec3 value = parse_bracketed_vec3(tokens);
            if(tokens.is_string(name, "v1")) {
                texture.v1 = value;
            } else if(tokens.is_string(name, "v2")) {
                texture.v2 = value;
            } else if(tokens.is_string(name, "dir")) {
                texture.dir = value;
            } else {
                tokens.fail("Unknown texture attribute.");
            }

        } else if(tokens.type_is_texture(type)) {

            auto value = parse_string_or_string_list(tokens);
            if(!parse_texture_texture_reference(tokens, name, texture, [&]() {
                   return parser.named_texture(tokens.to_string(value));
               })) {
                tokens.fail("Unknown texture attribute.");
            }

        } else if(tokens.type_is_rgb(type)) {

            Spectrum value = parse_bracketed_vec3(tokens);
            if(tokens.is_string(name, "value")) {
                texture.spectrum = value;
            } else if(!parse_texture_texture_reference(tokens, name, texture, [&]() {
                          return scene.add_const_texture(value);
                      })) {
                tokens.fail("Unknown texture attribute.");
            }

        } else if(tokens.type_is_bool(type)) {

            bool value = parse_bool_or_bool_list(tokens);
            if(tokens.is_string(name, "invert")) {
                texture.invert = value;
            } else {
                tokens.fail("Unknown texture attribute.");
            }

        } else if(tokens.type_is_int(type)) {

            i32 value = parse_int_or_int_list(tokens);
            if(tokens.is_string(name, "octaves")) {
                texture.octaves = value;
            } else if(tokens.is_string(name, "dimension")) {
                texture.dimension = value;
            } else {
                tokens.fail("Unknown texture attribute.");
            }

        } else {
            tokens.fail("Unknown texture attribute type.");
        }
    }

    auto id = scene.next_texture_id();
    parser.named_texture(tokens.to_string(tex_name), id);

    if(texture.type == Textures::Type::imagemap || texture.type == Textures::Type::ptex) {

        auto task =
            complete_texture_async(pool, parser.directory.clone(), move(filename), move(texture));
        scene.texture_tasks.insert(id, move(task));

    } else {
        scene.textures.insert(id, move(texture));
    }
}

static void parse_partial_scene_self_shape(Async::Pool<>& pool, Tokenizer& tokens,
                                           Partial_Scene& scene) {

    // Helper function, *not* a coroutine so OK to throw exceptions

    Parser& parser = scene.parser;

    auto kind = tokens.expect_quoted_string();
    auto material =
        parser.current_material().match(Overload{[](Material_ID id) { return id; },
                                                 [&parser](const String<Alloc>& name) {
                                                     try {
                                                         return parser.named_material(name.view());
                                                     } catch(Parser::Error) {
                                                         return Material_ID{};
                                                     }
                                                 }});
    auto& area_light = parser.current_area_light();

    if(tokens.is_string(kind, "trianglemesh")) {
        Mesh mesh;

        if(area_light.L != Vec3{0.0f}) {
            mesh.emission = area_light.L * area_light.scale;
        }
        mesh.mesh_to_instance = parser.current_transform();
        mesh.material = material;

        while(true) {
            if(!tokens.is_quote(tokens.peek())) break;
            tokens.expect_quote();
            auto type = tokens.next();
            auto name = tokens.next();
            tokens.expect_quote();
            if(tokens.is_string(name, "indices")) {
                mesh.indices = parse_int_list(tokens);
            } else if(tokens.is_string(name, "P")) {
                mesh.positions = parse_float_list(tokens);
            } else if(tokens.is_string(name, "N")) {
                mesh.normals = parse_float_list(tokens);
            } else if(tokens.is_string(name, "S")) {
                mesh.tangents = parse_float_list(tokens);
            } else if(tokens.is_string(name, "uv")) {
                mesh.uvs = parse_float_list(tokens);
            } else if(tokens.is_string(name, "alpha")) {
                if(tokens.type_is_texture(type)) {
                    mesh.alpha =
                        parser.named_texture(tokens.to_string(parse_string_or_string_list(tokens)));
                } else if(tokens.type_is_float(type)) {
                    mesh.alpha = scene.add_const_texture(parse_float_or_float_list(tokens));
                } else {
                    tokens.fail("Unknown alpha attribute type.");
                }
            } else if(tokens.is_string(name, "faceIndices")) {
                mesh.face_indices = parse_int_list(tokens);
            } else if(tokens.is_string(name, "st")) {
                warn("[PBRT] st attribute is deprecated.");
                parse_float_list(tokens);
            } else {
                tokens.fail("Unknown triangle mesh attribute.");
            }
        }
        if(mesh.positions.empty() || mesh.indices.empty()) {
            tokens.fail("Missing required attribute.");
        }

        transform_normals(mesh.normals, mesh.mesh_to_instance);
        if(parser.current_reverse_orientation()) {
            mesh.reverse_orientation();
        }

        auto id = scene.next_mesh_id();
        scene.meshes.insert(id, move(mesh));
        scene.add_shape(parser.current_object(), id);

    } else if(tokens.is_string(kind, "plymesh")) {
        String_View filename;
        Texture_ID alpha;
        while(true) {
            if(!tokens.is_quote(tokens.peek())) break;
            tokens.expect_quote();
            auto type = tokens.next();
            auto name = tokens.next();
            tokens.expect_quote();
            if(tokens.is_string(name, "filename")) {
                filename = tokens.to_string(parse_string_or_string_list(tokens));
            } else if(tokens.is_string(name, "displacement")) {
                tokens.ignore.displacement++;
                ignore_parameter(tokens, type);
            } else if(tokens.is_string(name, "edgelength")) {
                tokens.ignore.edgelength++;
                ignore_parameter(tokens, type);
            } else if(tokens.is_string(name, "alpha")) {
                if(tokens.type_is_texture(type)) {
                    alpha =
                        parser.named_texture(tokens.to_string(parse_string_or_string_list(tokens)));
                } else if(tokens.type_is_float(type)) {
                    alpha = scene.add_const_texture(parse_float_or_float_list(tokens));
                } else {
                    tokens.fail("Unknown alpha attribute type.");
                }
            } else {
                tokens.fail("Unknown ply mesh attribute.");
            }
        }
        if(filename.length() == 0) {
            tokens.fail("Missing required attribute.");
        }

        auto task = load_ply_async(pool, parser.current_state().clone(), parser.directory.clone(),
                                   filename.string<Alloc>(), material, alpha);
        auto id = scene.next_mesh_id();
        scene.mesh_tasks.insert(id, move(task));
        scene.add_shape(parser.current_object(), id);

    } else if(tokens.is_string(kind, "bilinearmesh")) {
        tokens.ignore.bilinear_mesh++;
        ignore_attributes(tokens);
    } else if(tokens.is_string(kind, "loopsubdiv")) {
        tokens.ignore.loop_subdiv_mesh++;
        ignore_attributes(tokens);
    } else if(tokens.is_string(kind, "curve")) {
        tokens.ignore.curve++;
        ignore_attributes(tokens);
    } else if(tokens.is_string(kind, "cylinder")) {
        // TODO bunny fur has cylinders
        tokens.ignore.cylinder++;
        ignore_attributes(tokens);
    } else if(tokens.is_string(kind, "disk")) {
        // TODO staircase 2, explosion, villa night, bunny cloud have disks
        tokens.ignore.disk++;
        ignore_attributes(tokens);
    } else if(tokens.is_string(kind, "sphere")) {
        // TODO veach MIS, night pavillion, explosion, pbrt book, bunny cloud, smoke plume have
        // spheres
        tokens.ignore.sphere++;
        ignore_attributes(tokens);
    } else {
        tokens.fail("Unknown shape type.");
    }
}

static Async::Task<Partial_Scene> parse_partial_scene(Async::Pool<>& pool, String<Alloc> directory,
                                                      String<Alloc> rel_path, Parser init);

static Async::Task<void> parse_partial_scene_self(Async::Pool<>& pool, Tokenizer& tokens,
                                                  Partial_Scene& scene, String<Alloc> filename);

static Async::Task<void> parse_partial_scene_include(Async::Pool<>& pool, Partial_Scene& scene,
                                                     String<Alloc> rel_path) {
    Tokenizer tokens;
    auto& parser = scene.parser;

    auto path = parser.directory.append<Alloc>(rel_path);

    if(auto file = co_await Async::read(pool, path.view()); file.ok()) {
        tokens.file = move(*file);
    } else {
        warn("[PBRT] failed to open included file %", path);
        co_return;
    }

    co_await parse_partial_scene_self(pool, tokens, scene,
                                      path.view().file_suffix().string<Alloc>());
}

static Parser::Area_Light parse_area_light(Tokenizer& tokens) {
    auto kind = tokens.expect_quoted_string();
    if(tokens.is_string(kind, "diffuse")) {
        Parser::Area_Light light;
        while(true) {
            if(!tokens.is_quote(tokens.peek())) break;
            tokens.expect_quote();
            auto type = tokens.next();
            auto name = tokens.next();
            tokens.expect_quote();
            if(tokens.is_string(name, "L")) {
                if(tokens.type_is_blackbody(type)) {
                    if(tokens.is_float(tokens.peek())) {
                        auto temp = tokens.expect_float();
                        light.L = builtin_blackbody(temp);
                    } else {
                        auto params = parse_float_list(tokens);
                        if(params.empty()) {
                            tokens.fail("Missing blackbody temperature parameter.");
                        }
                        light.L = builtin_blackbody(params[0]);
                        if(params.length() > 1) {
                            light.scale = params[1];
                        }
                    }
                } else if(tokens.type_is_rgb(type) || tokens.type_is_vec3(type)) {
                    light.L = parse_bracketed_vec3(tokens);
                } else {
                    tokens.fail("Unknown area light L type.");
                }
            } else if(tokens.is_string(name, "filename")) {
                warn("[PBRT] ignoring emissive texture filename.");
                ignore_parameter(tokens, type);
            } else if(tokens.is_string(name, "scale")) {
                light.scale = parse_float_or_float_list(tokens);
            } else if(tokens.is_string(name, "twosided")) {
                light.two_sided = parse_bool_or_bool_list(tokens);
            } else {
                tokens.fail("Unknown area light attribute.");
            }
        }
        return light;
    }
    tokens.fail("Unknown area light source kind.");
}

static Async::Task<void> parse_partial_scene_self(Async::Pool<>& pool, Tokenizer& tokens,
                                                  Partial_Scene& scene, String<Alloc> filename) {

    Parser& parser = scene.parser;

    // Parser exceptions must not escape the coroutine
    try {
        while(true) {
            auto token = tokens.next();
            if(token.eof()) break;

            if(tokens.is_string(token, "Transform")) {
                parser.current_transform() = parse_transform(tokens);
            } else if(tokens.is_string(token, "Identity")) {
                parser.current_transform() = Mat4::I;
            } else if(tokens.is_string(token, "CoordinateSystem")) {
                auto name = tokens.to_string(tokens.expect_quoted_string());
                parser.named_transform(name, parser.current_transform());
            } else if(tokens.is_string(token, "CoordSysTransform")) {
                auto name = tokens.to_string(tokens.expect_quoted_string());
                parser.current_transform() = parser.named_transform(name);
            } else if(tokens.is_string(token, "ConcatTransform")) {
                parser.current_transform() = parser.current_transform() * parse_transform(tokens);
            } else if(tokens.is_string(token, "Scale")) {
                Vec3 v = parse_vec3(tokens);
                parser.current_transform() = parser.current_transform() * Mat4::scale(v);
            } else if(tokens.is_string(token, "Rotate")) {
                f32 degrees = tokens.expect_float();
                Vec3 axis = parse_vec3(tokens);
                parser.current_transform() =
                    parser.current_transform() * Mat4::rotate(degrees, axis);
            } else if(tokens.is_string(token, "Translate")) {
                Vec3 v = parse_vec3(tokens);
                parser.current_transform() = parser.current_transform() * Mat4::translate(v);
            } else if(tokens.is_string(token, "LookAt")) {
                Vec3 eye = parse_vec3(tokens);
                Vec3 look = parse_vec3(tokens);
                Vec3 up = parse_vec3(tokens);
                parser.current_transform() =
                    parser.current_transform() * Mat4::look_at(eye, look, up);
            } else if(parser.world_begun) {

                if(tokens.is_string(token, "Include")) {
                    auto child = tokens.to_string(tokens.expect_quoted_string());

                    // Synchronous: include needs to access the same command stream
                    co_await parse_partial_scene_include(pool, scene, child.string<Alloc>());

                    if(tokens.is_quote(tokens.peek())) {
                        warn("[PBRT] ignoring extra attributes after include.");
                        ignore_attributes(tokens);
                    }
                } else if(tokens.is_string(token, "Import")) {
                    auto child = tokens.to_string(tokens.expect_quoted_string());

                    // Asynchronous: can continue parsing while the import is in progress.
                    auto import = parse_partial_scene(pool, parser.directory.clone(),
                                                      child.string<Alloc>(), parser.fork());
                    scene.import_tasks.push(move(import));

                } else if(tokens.is_string(token, "Shape")) {
                    parse_partial_scene_self_shape(pool, tokens, scene);
                } else if(tokens.is_string(token, "ObjectBegin")) {
                    auto name = tokens.expect_quoted_string();
                    auto id = scene.next_object_id();
                    scene.objects.insert(id, Object{parser.current_transform(), {}, {}});
                    parser.named_object(tokens.to_string(name), id);
                    parser.push_object(id);
                    parser.push_state();
                } else if(tokens.is_string(token, "ObjectEnd")) {
                    parser.pop_state();
                    parser.pop_object();
                } else if(tokens.is_string(token, "ObjectInstance")) {
                    auto name = tokens.expect_quoted_string();
                    auto id = parser.named_object(tokens.to_string(name));
                    scene.add_instance(parser.current_object(),
                                       Instance{parser.current_transform(), id});
                } else if(tokens.is_string(token, "AttributeBegin")) {
                    parser.push_state();
                } else if(tokens.is_string(token, "AttributeEnd")) {
                    parser.pop_state();
                } else if(tokens.is_string(token, "TransformBegin")) {
                    warn("[PBRT] TransformBegin is deprecated.");
                    parser.push_state();
                } else if(tokens.is_string(token, "TransformEnd")) {
                    warn("[PBRT] TransformEnd is deprecated.");
                    parser.pop_state();
                } else if(tokens.is_string(token, "ReverseOrientation")) {
                    parser.current_reverse_orientation() = !parser.current_reverse_orientation();
                } else if(tokens.is_string(token, "MakeNamedMaterial")) {
                    parse_partial_scene_self_material_named(tokens, scene);
                } else if(tokens.is_string(token, "NamedMaterial")) {
                    auto name = tokens.to_string(tokens.expect_quoted_string());
                    parser.current_material() =
                        Variant<Material_ID, String<Alloc>>{name.string<Alloc>()};
                } else if(tokens.is_string(token, "AreaLightSource")) {
                    parser.current_area_light() = parse_area_light(tokens);
                } else if(tokens.is_string(token, "Texture")) {
                    parse_partial_scene_self_texture(pool, tokens, scene);
                } else if(tokens.is_string(token, "Material")) {
                    parser.current_material() = Variant<Material_ID, String<Alloc>>{
                        parse_partial_scene_self_material(tokens, scene)};
                } else if(tokens.is_string(token, "LightSource")) {
                    parse_partial_scene_self_light(pool, tokens, scene);
                } else if(tokens.is_string(token, "MakeNamedMedium")) {
                    tokens.ignore.make_named_medium++;
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "MediumInterface")) {
                    tokens.ignore.medium_interface++;
                    ignore_defn(tokens, 2);
                } else if(tokens.is_string(token, "WorldEnd")) {
                    warn("[PBRT] WorldEnd is deprecated.");
                } else if(tokens.is_string(token, "Volume")) {
                    warn("[PBRT] Volume is deprecated.");
                    ignore_defn(tokens);
                } else {
                    tokens.fail("Unknown identifier (post-worldbegin).");
                }

            } else {

                if(tokens.is_string(token, "Camera")) {
                    scene.camera = Camera{parser.current_transform()};
                    parser.named_transform(String_View{"camera"}, parser.current_transform());
                    warn("[PBRT] ignoring camera parameters...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "WorldBegin")) {
                    parser.world_begun = true;
                    parser.reset_state();
                } else if(tokens.is_string(token, "Option")) {
                    tokens.expect_quote();
                    auto type = tokens.next();
                    auto name = tokens.next();
                    warn("[PBRT] ignoring option %...", tokens.to_string(name));
                    tokens.expect_quote();
                    ignore_type(tokens, type);
                } else if(tokens.is_string(token, "Integrator")) {
                    warn("[PBRT] ignoring integrator...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "Sampler")) {
                    warn("[PBRT] ignoring sampler...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "PixelFilter")) {
                    warn("[PBRT] ignoring pixel filter...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "Film")) {
                    warn("[PBRT] ignoring film...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "ColorSpace")) {
                    warn("[PBRT] ignoring color space...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "Accelerator")) {
                    warn("[PBRT] ignoring accelerator...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "MakeNamedMedium")) {
                    warn("[PBRT] ignoring global named medium...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "MediumInterface")) {
                    warn("[PBRT] ignoring global medium interface...");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "SurfaceIntegrator")) {
                    warn("[PBRT] SurfaceIntegrator is deprecated.");
                    ignore_defn(tokens);
                } else if(tokens.is_string(token, "VolumeIntegrator")) {
                    warn("[PBRT] VolumeIntegrator is deprecated.");
                    ignore_defn(tokens);
                } else {
                    tokens.fail("Unknown identifier (pre-worldbegin).");
                }
            }
        }
    } catch(Parser::Error err) {
        warn("[PBRT] failed to parse at %:% - %", filename, err.line, String_View{err.msg});
    }

    co_return;
}

static Async::Task<Partial_Scene> parse_partial_scene(Async::Pool<>& pool, String<Alloc> directory,
                                                      String<Alloc> rel_path, Parser init) {
    co_await pool.suspend();

    Partial_Scene scene{move(init)};
    Tokenizer tokens;

    auto& parser = scene.parser;

    parser.directory = move(directory);
    auto path = parser.directory.append<Alloc>(rel_path);

    if(auto file = co_await Async::read(pool, path.view()); file.ok()) {
        tokens.file = move(*file);
    } else {
        warn("[PBRT] failed to open file %", path);
        co_return scene;
    }

    co_await parse_partial_scene_self(pool, tokens, scene,
                                      path.view().file_suffix().string<Alloc>());

    if(tokens.ignore.displacement)
        warn("[PBRT] ignored % plymesh displacement attributes.", tokens.ignore.displacement);
    if(tokens.ignore.edgelength)
        warn("[PBRT] ignored % plymesh edgelength attributes.", tokens.ignore.edgelength);
    if(tokens.ignore.bilinear_mesh)
        warn("[PBRT] ignored % bilinear meshes.", tokens.ignore.bilinear_mesh);
    if(tokens.ignore.loop_subdiv_mesh)
        warn("[PBRT] ignored % loop subdiv meshes.", tokens.ignore.loop_subdiv_mesh);
    if(tokens.ignore.curve) warn("[PBRT] ignored % curves.", tokens.ignore.curve);
    if(tokens.ignore.cylinder) warn("[PBRT] ignored % cylinders.", tokens.ignore.cylinder);
    if(tokens.ignore.disk) warn("[PBRT] ignored % disks.", tokens.ignore.disk);
    if(tokens.ignore.sphere) warn("[PBRT] ignored % spheres.", tokens.ignore.sphere);
    if(tokens.ignore.make_named_medium)
        warn("[PBRT] ignored % named medium definitions.", tokens.ignore.make_named_medium);
    if(tokens.ignore.medium_interface)
        warn("[PBRT] ignored % named medium instances.", tokens.ignore.medium_interface);

    co_return scene;
}

Async::Task<Scene> load(Async::Pool<>& pool, String_View path) {
    info("Loading scene from %...", path);
    auto scene = co_await parse_partial_scene(pool, path.remove_file_suffix().string<Alloc>(),
                                              path.file_suffix().string<Alloc>(), Parser{0});
    co_return co_await scene.to_scene();
}

void Mesh::reverse_orientation() {
    if(normals.length()) {
        for(u64 i = 0; i < normals.length(); i++) {
            normals[i] = -normals[i];
        }
    } else {
        for(u64 i = 0; i < indices.length(); i += 3) {
            swap(indices[i], indices[i + 2]);
        }
    }
}

} // namespace PBRT
