
#ifndef _HKR_BUILTINS_HLSL
#define _HKR_BUILTINS_HLSL

#define RayTracingPositionFetchKHR 5336
#define HitTriangleVertexPositionsKHR 5335
#define BuiltInObjectToWorldKHR 5330
#define BuiltInWorldToObjectKHR 5331
#define RayGeometryIndexKHR 5352
#define InstanceCustomIndexKHR 5327
#define WorldRayDirectionKHR 5322
#define PrimitiveID 7

#define OpControlBarrier 224

#ifdef HIT_POSITIONS
[[vk::ext_extension("SPV_KHR_ray_tracing_position_fetch")]]
[[vk::ext_capability(RayTracingPositionFetchKHR)]]
[[vk::ext_builtin_input(HitTriangleVertexPositionsKHR)]]
static const f32point g_hit_triangle_vertex_positions[3];
#endif

[[vk::ext_builtin_input(WorldRayDirectionKHR)]]
static const f32dir g_world_ray_direction;

[[vk::ext_builtin_input(BuiltInObjectToWorldKHR)]]
static const f32m4x3 g_object_to_world;

[[vk::ext_builtin_input(BuiltInWorldToObjectKHR)]]
static const f32m4x3 g_world_to_object;

[[vk::ext_builtin_input(RayGeometryIndexKHR)]]
static const u32 g_geometry_index;

[[vk::ext_builtin_input(InstanceCustomIndexKHR)]]
static const u32 g_instance_custom_index;

[[vk::ext_builtin_input(PrimitiveID)]]
static const u32 g_primitive_id;

[[vk::ext_instruction(OpControlBarrier)]]
void barrier(u32 execution_scope, u32 memory_scope, u32 memory_semantics);

#endif // _HKR_BUILTINS_HLSL