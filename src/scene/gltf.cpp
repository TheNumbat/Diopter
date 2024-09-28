
#include <tinygltf/tiny_gltf.h>

#include "gltf.h"

namespace GLTF {

struct Loader {
    Vec<Async::Task<Mesh>, Alloc> meshes;
    Vec<Async::Task<Texture>, Alloc> textures;
};

static Async::Task<Primitive> load_primitive(Async::Pool<>& pool, const tinygltf::Model& gmodel,
                                             const tinygltf::Primitive& gprimitive) {

    co_await pool.suspend();

    Primitive mesh;

    bool convertedToTriangleList = false;
    {
        const auto& indicesAccessor = gmodel.accessors[gprimitive.indices];
        const auto& bufferView = gmodel.bufferViews[indicesAccessor.bufferView];
        const auto& buffer = gmodel.buffers[bufferView.buffer];
        const auto dataAddress =
            buffer.data.data() + bufferView.byteOffset + indicesAccessor.byteOffset;
        const auto byteStride = indicesAccessor.ByteStride(bufferView);
        const auto count = indicesAccessor.count;

        switch(indicesAccessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            for(u64 i = 0; i < count; i++) {
                mesh.indices.push(*reinterpret_cast<const i8*>(dataAddress + byteStride * i));
            }
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            for(u64 i = 0; i < count; i++) {
                mesh.indices.push(*reinterpret_cast<const u8*>(dataAddress + byteStride * i));
            }
            break;

        case TINYGLTF_COMPONENT_TYPE_SHORT:
            for(u64 i = 0; i < count; i++) {
                mesh.indices.push(*reinterpret_cast<const i16*>(dataAddress + byteStride * i));
            }
            break;

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            for(u64 i = 0; i < count; i++) {
                mesh.indices.push(*reinterpret_cast<const u16*>(dataAddress + byteStride * i));
            }
            break;

        case TINYGLTF_COMPONENT_TYPE_INT:
            for(u64 i = 0; i < count; i++) {
                mesh.indices.push(*reinterpret_cast<const i32*>(dataAddress + byteStride * i));
            }
            break;

        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            for(u64 i = 0; i < count; i++) {
                mesh.indices.push(*reinterpret_cast<const u32*>(dataAddress + byteStride * i));
            }
            break;
        default: break;
        }
    }

    switch(gprimitive.mode) {
    case TINYGLTF_MODE_TRIANGLE_FAN: {
        if(!convertedToTriangleList) {
            convertedToTriangleList = true;
            auto triangleFan = move(mesh.indices);
            mesh.indices.clear();

            for(u64 i = 2; i < triangleFan.length(); ++i) {
                mesh.indices.push(triangleFan[0]);
                mesh.indices.push(triangleFan[i - 1]);
                mesh.indices.push(triangleFan[i]);
            }
        }
    }
    case TINYGLTF_MODE_TRIANGLE_STRIP: {
        if(!convertedToTriangleList) {
            convertedToTriangleList = true;
            auto triangleStrip = move(mesh.indices);
            mesh.indices.clear();

            for(u64 i = 2; i < triangleStrip.length(); ++i) {
                mesh.indices.push(triangleStrip[i - 2]);
                mesh.indices.push(triangleStrip[i - 1]);
                mesh.indices.push(triangleStrip[i]);
            }
        }
    }
    case TINYGLTF_MODE_TRIANGLES: {
        for(const auto& attribute : gprimitive.attributes) {
            const auto attribAccessor = gmodel.accessors[attribute.second];
            const auto& bufferView = gmodel.bufferViews[attribAccessor.bufferView];
            const auto& buffer = gmodel.buffers[bufferView.buffer];
            const auto dataPtr =
                buffer.data.data() + bufferView.byteOffset + attribAccessor.byteOffset;
            const auto byte_stride = attribAccessor.ByteStride(bufferView);
            const auto count = attribAccessor.count;

            if(attribute.first == "POSITION") {
                switch(attribAccessor.type) {
                case TINYGLTF_TYPE_VEC3: {
                    switch(attribAccessor.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_FLOAT:
                        for(u64 i = 0; i < count; i++) {
                            const f32* values =
                                reinterpret_cast<const f32*>(dataPtr + i * byte_stride);
                            mesh.positions.push(values[0]);
                            mesh.positions.push(values[1]);
                            mesh.positions.push(values[2]);
                        }
                    }
                    break;
                case TINYGLTF_COMPONENT_TYPE_DOUBLE: {
                    switch(attribAccessor.type) {
                    case TINYGLTF_TYPE_VEC3: {
                        for(u64 i = 0; i < count; i++) {
                            const f64* values =
                                reinterpret_cast<const f64*>(dataPtr + i * byte_stride);
                            mesh.positions.push(static_cast<f32>(values[0]));
                            mesh.positions.push(static_cast<f32>(values[1]));
                            mesh.positions.push(static_cast<f32>(values[2]));
                        }
                    } break;
                    default: break;
                    }
                    break;
                default: break;
                }
                } break;
                }
            }

            if(attribute.first == "TANGENT") {
                switch(attribAccessor.type) {
                case TINYGLTF_TYPE_VEC4: {
                    switch(attribAccessor.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_FLOAT:
                        for(u64 i = 0; i < count; i++) {
                            const f32* values =
                                reinterpret_cast<const f32*>(dataPtr + i * byte_stride);
                            mesh.tangents.push(values[0]);
                            mesh.tangents.push(values[1]);
                            mesh.tangents.push(values[2]);
                            if(values[3] < 0.0f) mesh.flip_bitangent = true;
                        }
                    }
                    break;
                case TINYGLTF_COMPONENT_TYPE_DOUBLE: {
                    switch(attribAccessor.type) {
                    case TINYGLTF_TYPE_VEC4: {
                        for(u64 i = 0; i < count; i++) {
                            const f64* values =
                                reinterpret_cast<const f64*>(dataPtr + i * byte_stride);
                            mesh.tangents.push(static_cast<f32>(values[0]));
                            mesh.tangents.push(static_cast<f32>(values[1]));
                            mesh.tangents.push(static_cast<f32>(values[2]));
                            if(values[3] < 0.0) mesh.flip_bitangent = true;
                        }
                    } break;
                    default: break;
                    }
                    break;
                default: break;
                }
                } break;
                }
            }

            if(attribute.first == "NORMAL") {
                switch(attribAccessor.type) {
                case TINYGLTF_TYPE_VEC3: {
                    switch(attribAccessor.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
                        for(u64 i = 0; i < count; i++) {
                            const f32* values =
                                reinterpret_cast<const f32*>(dataPtr + i * byte_stride);
                            mesh.normals.push(values[0]);
                            mesh.normals.push(values[1]);
                            mesh.normals.push(values[2]);
                        }
                    } break;
                    case TINYGLTF_COMPONENT_TYPE_DOUBLE: {
                        for(u64 i = 0; i < count; i++) {
                            const f64* values =
                                reinterpret_cast<const f64*>(dataPtr + i * byte_stride);
                            mesh.normals.push(static_cast<f32>(values[0]));
                            mesh.normals.push(static_cast<f32>(values[1]));
                            mesh.normals.push(static_cast<f32>(values[2]));
                        }
                    } break;
                    default: {
                        warn("[gltf] Unhandled component type for normal.");
                        break;
                    }
                    }
                } break;
                default: {
                    warn("[gltf] Unhandled vector type for normal.");
                    break;
                }
                }
            }

            if(attribute.first == "TEXCOORD_0") {
                switch(attribAccessor.type) {
                case TINYGLTF_TYPE_VEC2: {
                    switch(attribAccessor.componentType) {
                    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
                        for(u64 i = 0; i < count; i++) {
                            const f32* values =
                                reinterpret_cast<const f32*>(dataPtr + i * byte_stride);
                            mesh.uvs.push(values[0]);
                            mesh.uvs.push(values[1]);
                        }
                    } break;
                    case TINYGLTF_COMPONENT_TYPE_DOUBLE: {
                        for(u64 i = 0; i < count; i++) {
                            const f64* values =
                                reinterpret_cast<const f64*>(dataPtr + i * byte_stride);
                            mesh.uvs.push(static_cast<f32>(values[0]));
                            mesh.uvs.push(static_cast<f32>(values[1]));
                        }
                    } break;
                    default: {
                        warn("[gltf] Unrecognized vector type for UV.");
                    }
                    }
                } break;
                default: {
                    warn("[gltf] Unreconized component type for UV.");
                    break;
                }
                }
            }
        }
        break;

    default: {
        warn("[gltf] Unrecognized geometry mode.");
        break;
    }

    // These aren't triangles:
    case TINYGLTF_MODE_POINTS:
    case TINYGLTF_MODE_LINE:
    case TINYGLTF_MODE_LINE_LOOP: {
        warn("[gltf] Geometry is not triangle-based, ignoring.");
        break;
    }
    }
    }

    mesh.material = gprimitive.material;
    co_return mesh;
}

static Async::Task<Mesh> load_mesh(Async::Pool<>& pool, const tinygltf::Model& gmodel,
                                   const tinygltf::Mesh& gmesh) {
    co_await pool.suspend();
    Vec<Async::Task<Primitive>, Alloc> primitives;
    for(const auto& gprimitive : gmesh.primitives) {
        primitives.push(load_primitive(pool, gmodel, gprimitive));
    }
    Mesh out;
    for(auto& task : primitives) {
        out.primitives.push(co_await task);
    }
    co_return out;
}

static u32 load_node(Scene& scene, const tinygltf::Model& model, u32 node_idx) {

    auto& gnode = model.nodes[node_idx];

    Node out;

    out.mesh = gnode.mesh;
    out.light = gnode.light;

    for(u64 i = 0; i < 16 && i < gnode.matrix.size(); i++) {
        out.node_to_parent.data[i] = static_cast<f32>(gnode.matrix[i]);
    }

    for(auto& child : gnode.children) {
        out.children.push(load_node(scene, model, child));
    }

    u32 id = static_cast<u32>(scene.nodes.length());
    scene.nodes.push(std::move(out));
    return id;
}

Async::Task<Texture> load_texture(Async::Pool<>& pool, const tinygltf::Model& model,
                                  const tinygltf::Texture& texture) {

    if(static_cast<u64>(texture.source) >= model.images.size()) co_return {};

    const auto& image = model.images[texture.source];
    const auto size = image.component * image.width * image.height * sizeof(u8);

    Vec<u8, Alloc> data(size);
    data.unsafe_fill();

    Libc::memcpy(data.data(), image.image.data(), size);

    co_return Texture{std::move(data), static_cast<u32>(image.width),
                      static_cast<u32>(image.height), static_cast<u32>(image.component)};
}

Async::Task<Scene> load(Async::Pool<>& pool, String_View file) {

    co_await pool.suspend();

    Scene scene;

    tinygltf::Model model;
    tinygltf::TinyGLTF gloader;

    std::string err, warn;

    bool ok = false;
    if(file.file_extension() == "glb"_v) {
        ok = gloader.LoadBinaryFromFile(
            &model, &err, &warn,
            std::string{reinterpret_cast<const char*>(file.data()), file.length()});
    } else if(file.file_extension() == "gltf"_v) {
        ok = gloader.LoadASCIIFromFile(
            &model, &err, &warn,
            std::string{reinterpret_cast<const char*>(file.data()), file.length()});
    }

    if(!warn.empty()) {
        warn("[gltf] Warning loading %: %.", file, String_View{warn.c_str()});
    }

    if(!err.empty()) {
        warn("[gltf] Error loading %: %.", file, String_View{err.c_str()});
        co_return scene;
    }

    if(!ok) {
        warn("[gltf] Failed to parse %.");
        co_return scene;
    }

    Loader loader;

    for(const auto& mesh : model.meshes) {
        loader.meshes.push(load_mesh(pool, model, mesh));
    }
    for(const auto& texture : model.textures) {
        loader.textures.push(load_texture(pool, model, texture));
    }

    for(auto& gscene : model.scenes) {
        for(auto& groot : gscene.nodes) {
            scene.top_level_nodes.push(load_node(scene, model, groot));
        }
    }

    for(const auto& gmat : model.materials) {

        const auto& basecolorfactor = gmat.pbrMetallicRoughness.baseColorFactor;
        const auto emissivefactor = gmat.emissiveFactor;

        Material mat;

        mat.base_color =
            Vec4{static_cast<f32>(basecolorfactor[0]), static_cast<f32>(basecolorfactor[1]),
                 static_cast<f32>(basecolorfactor[2]), static_cast<f32>(basecolorfactor[3])};
        mat.base_color_texture = gmat.pbrMetallicRoughness.baseColorTexture.index;

        if(gmat.alphaMode != "OPAQUE") mat.alpha_cutoff = static_cast<f32>(gmat.alphaCutoff);
        mat.double_sided = gmat.doubleSided;

        mat.emissive =
            Vec3{static_cast<f32>(emissivefactor[0]), static_cast<f32>(emissivefactor[1]),
                 static_cast<f32>(emissivefactor[2])};
        mat.emissive_texture = gmat.emissiveTexture.index;

        mat.metallic = static_cast<f32>(gmat.pbrMetallicRoughness.metallicFactor);
        mat.roughness = static_cast<f32>(gmat.pbrMetallicRoughness.roughnessFactor);
        mat.metallic_roughness_texture = gmat.pbrMetallicRoughness.metallicRoughnessTexture.index;

        mat.normal_scale = static_cast<f32>(gmat.normalTexture.scale);
        mat.normal_texture = gmat.normalTexture.index;

        scene.materials.push(std::move(mat));
    }

    for(const auto& glight : model.lights) {
        Light light;

        if(glight.type == "point") {
            light.type = Light::Type::point;
        } else if(glight.type == "spot") {
            light.type = Light::Type::spot;
        } else if(glight.type == "directional") {
            light.type = Light::Type::directional;
        } else {
            warn("[gltf] Unrecognized light type %.", String_View{glight.type.c_str()});
        }

        const auto& color = glight.color;
        if(color.size() == 3) {
            light.color = Vec3{static_cast<f32>(color[0]), static_cast<f32>(color[1]),
                               static_cast<f32>(color[2])};
        }
        light.intensity = static_cast<f32>(glight.intensity);
        light.range = static_cast<f32>(glight.range);
        light.inner_cone_angle = static_cast<f32>(glight.spot.innerConeAngle);
        light.outer_cone_angle = static_cast<f32>(glight.spot.outerConeAngle);

        scene.lights.push(std::move(light));
    }

    for(auto& task : loader.meshes) {
        scene.meshes.push(co_await task);
    }
    for(auto& task : loader.textures) {
        scene.textures.push(co_await task);
    }

    co_return scene;
}

} // namespace GLTF
