#include "backends/cpp-entt/runtime.hpp"

#include "backends/cpp-entt/raylib_io.hpp"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory_resource>
#include <numbers>
#include <numeric>
#include <raymath.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cactus::runtime::entt_backend {

namespace {
RenderDebugState& render_debug_state_storage() noexcept {
    static RenderDebugState state;
    return state;
}

Camera2D& active_camera_2d_storage() noexcept {
    static Camera2D cam{.offset = {}, .target = {}, .rotation = 0.0F, .zoom = 1.0F};
    return cam;
}

Camera3D& active_camera_3d_storage() noexcept {
    static Camera3D cam{.position   = {.x = 6.0F, .y = 6.0F, .z = 6.0F},
                        .target     = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                        .up         = {.x = 0.0F, .y = 1.0F, .z = 0.0F},
                        .fovy       = 45.0F,
                        .projection = CAMERA_PERSPECTIVE};
    return cam;
}

EditorHitTestImpl& hit_test_impl_storage() noexcept {
    static EditorHitTestImpl impl;
    return impl;
}

EditorSpawnImpl& spawn_impl_storage() noexcept {
    static EditorSpawnImpl impl;
    return impl;
}

EditorRaycastImpl& raycast_impl_storage() noexcept {
    static EditorRaycastImpl impl;
    return impl;
}

EditorActiveModeImpl& active_mode_impl_storage() noexcept {
    static EditorActiveModeImpl impl;
    return impl;
}

EditorIsActiveImpl& is_active_impl_storage() noexcept {
    static EditorIsActiveImpl impl;
    return impl;
}

PointerCandidatesImpl& pointer_window_candidates_impl_storage() noexcept {
    static PointerCandidatesImpl impl;
    return impl;
}

PointerCandidatesImpl& pointer_world_candidates_impl_storage() noexcept {
    static PointerCandidatesImpl impl;
    return impl;
}

entt::entity& pointer_hovered_entity_storage() noexcept {
    static entt::entity entity{entt::null};
    return entity;
}

entt::entity& pointer_captured_entity_storage() noexcept {
    static entt::entity entity{entt::null};
    return entity;
}

constexpr int kPointerPrimaryMouseButton = 0;  // raylib MOUSE_BUTTON_LEFT

struct SpriteSubmission {
    Vector2 position{};
    Vector2 size{};
    Color color{};
    int runtime_id{-1};
    int layer{0};
};

struct MeshSubmission {
    Vector3 position{};
    Quat rotation{};
    Vector3 scale{};
    int mesh_runtime_id{-1};
    int material_runtime_id{-1};
    Color diffuse_color{WHITE};
};

struct ModelSubmission {
    Vector3 position{};
    Quat rotation{};
    Vector3 scale{};
    int model_runtime_id{-1};
    bool cast_shadow{true};
    // Animated submissions carry the entity's ModelAnimator pose; the flush
    // re-poses the shared model per submission (dsl-model-animation D1).
    int clip{0};
    float time{0.0F};
    bool animated{false};
};

struct PointLightSubmission {
    Vector3 position{};
    Color color{};
    float intensity{1.0F};
    float range{10.0F};
};

struct TextSubmission2D {
    Vector2 position{};
    float rotation_deg{0.0F};
    int font_size{32};
    Color color{};
    std::string text;
};

struct TextSubmission3D {
    uint32_t entity_id{0};
    Vector3 position{};
    Quat rotation{};
    Vector3 scale{};
    int font_size{1};
    Color color{};
    std::string text;
};

// Window-space HUD text (dsl-model-animation D5): window-global, top-left
// origin pixels, flushed after all viewport rendering so labels overlay both
// 2D and 3D content in any WorldTransform flavor.
struct ScreenLabelSubmission {
    Vector2 position{};
    int font_size{32};
    Color color{};
    std::string text;
};

struct TextLabel3DEntry {
    RenderTexture2D rt{};
    std::string cached_text;
    int cached_font_size{0};
    Color cached_color{};
    bool loaded{false};
};

struct TextureResourceEntry {
    Texture2D texture{};
    bool loaded{false};
    bool owned{false};
};

struct MeshResourceEntry {
    Mesh mesh{};
    // Declared asset path/id, filled on first submission (mirrors
    // ModelResourceEntry::path) so the lazy-load in ensure_mesh_resource can
    // select placeholder geometry by keyword match.
    std::string path;
    bool loaded{false};
    bool owned{false};
};

struct MaterialResourceEntry {
    Material material{};
    bool loaded{false};
    bool owned{false};
    Color diffuse_color{WHITE};
    bool diffuse_color_set{false};
};

// Model assets materialize lazily (dsl-model-assets D3/D4): registration only
// stores the path; the GLB is loaded on first render-time use, and a failed
// load is recorded so it is never retried.
struct ModelResourceEntry {
    Model model{};
    std::string path;
    // Clip data loads lazily on first animated draw or introspection call and
    // is freed in clear_model_store (dsl-model-animation D4).
    ModelAnimation* animations{nullptr};
    int animation_count{0};
    bool animations_load_attempted{false};
    // Skeleton within the bone limit: materials bound to the skinned shader.
    bool skinned{false};
    bool loaded{false};
    bool failed{false};
};

std::pmr::unsynchronized_pool_resource& render_queue_resource() noexcept {
    static std::pmr::unsynchronized_pool_resource resource;
    return resource;
}

std::pmr::vector<SpriteSubmission>& sprite_queue() noexcept {
    static std::pmr::vector<SpriteSubmission> queue{&render_queue_resource()};
    return queue;
}

std::pmr::vector<MeshSubmission>& mesh_queue() noexcept {
    static std::pmr::vector<MeshSubmission> queue{&render_queue_resource()};
    return queue;
}

std::pmr::vector<ModelSubmission>& model_queue() noexcept {
    static std::pmr::vector<ModelSubmission> queue{&render_queue_resource()};
    return queue;
}

std::pmr::vector<PointLightSubmission>& point_light_queue() noexcept {
    static std::pmr::vector<PointLightSubmission> queue{&render_queue_resource()};
    return queue;
}

std::vector<TextSubmission2D>& text_2d_queue() noexcept {
    static std::vector<TextSubmission2D> queue;
    return queue;
}

std::vector<TextSubmission3D>& text_3d_queue() noexcept {
    static std::vector<TextSubmission3D> queue;
    return queue;
}

std::vector<ScreenLabelSubmission>& screen_label_queue() noexcept {
    static std::vector<ScreenLabelSubmission> queue;
    return queue;
}

std::unordered_map<uint32_t, TextLabel3DEntry>& text_label_3d_cache() noexcept {
    static std::unordered_map<uint32_t, TextLabel3DEntry> cache;
    return cache;
}

std::unordered_map<int, TextureResourceEntry>& textures() noexcept {
    static std::unordered_map<int, TextureResourceEntry> entries;
    return entries;
}

std::unordered_map<int, MeshResourceEntry>& meshes() noexcept {
    static std::unordered_map<int, MeshResourceEntry> entries;
    return entries;
}

std::unordered_map<int, MaterialResourceEntry>& materials() noexcept {
    static std::unordered_map<int, MaterialResourceEntry> entries;
    return entries;
}

std::unordered_map<int, ModelResourceEntry>& models() noexcept {
    static std::unordered_map<int, ModelResourceEntry> entries;
    return entries;
}

// Handles that already produced a missing-model diagnostic (at most one per asset).
std::unordered_set<std::uint32_t>& diagnosed_missing_model_handles() noexcept {
    static std::unordered_set<std::uint32_t> handles;
    return handles;
}

// (model runtime id, clip index) pairs that already produced an invalid-clip
// diagnostic (at most one per pair, dsl-model-animation D3).
std::unordered_set<std::uint64_t>& diagnosed_invalid_clips() noexcept {
    static std::unordered_set<std::uint64_t> keys;
    return keys;
}

constexpr int kMaxMeshLights  = 4;
constexpr int kPointLightType = 1;

// Fixed tessellation for placeholder sphere geometry (design.md risk
// mitigation: a shared default for every sphere-shaped mesh asset, not
// per-entity-tunable, matching GenMeshCube's fixed 1x1x1 unit size).
// Only used by the real (non-fake) GenMeshSphere call below; the
// CACTUS_RAYLIB_FAKE headless build never reaches it.
#ifndef CACTUS_RAYLIB_FAKE
constexpr int kPlaceholderSphereRings  = 16;
constexpr int kPlaceholderSphereSlices = 16;
#endif

// Maximum bones the skinned shader supports; must match MAX_BONES in the
// skinned vertex shader (128 mat4 = 512 vec4 uniform components, within the
// GL 3.3 guaranteed 1024). Models exceeding this render unskinned (D2).
constexpr int kMaxSkinningBones = 128;

// raylib 6.0 bakes glTF clips at GLTF_FRAMERATE = 60 keyframes per second
// (rmodels.c), so animation frame = time in seconds * this rate. Pinned here;
// re-verify against GLTF_FRAMERATE on any raylib upgrade.
constexpr float kGltfKeyframesPerSecond = 60.0F;

#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
constexpr const char* kLightingVertexShader = R"(#version 100

attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kLightingFragmentShader = R"(#version 100

precision mediump float;

varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

#define MAX_LIGHTS 4
#define LIGHT_POINT 1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

void main()
{
    vec4 texelColor = texture2D(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1 && lights[i].type == LIGHT_POINT)
        {
            vec3 light = normalize(lights[i].position - fragPosition);
            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb*NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);
            specular += specCo*lights[i].color.rgb;
        }
    }

    vec4 finalColor = texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0));
    finalColor += texelColor*(ambient/10.0)*colDiffuse;
    gl_FragColor = pow(finalColor, vec4(1.0/2.2));
}
)";

// Skinned variant of the lit vertex shader (dsl-model-animation D2): bone
// attributes skin position and normal; the fragment shader is shared.
// MAX_BONES must match kMaxSkinningBones.
constexpr const char* kSkinnedLightingVertexShader = R"(#version 100

attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;
attribute vec4 vertexBoneIndices;
attribute vec4 vertexBoneWeights;

#define MAX_BONES 128

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 boneMatrices[MAX_BONES];

varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;

void main()
{
    int boneIndex0 = int(vertexBoneIndices.x);
    int boneIndex1 = int(vertexBoneIndices.y);
    int boneIndex2 = int(vertexBoneIndices.z);
    int boneIndex3 = int(vertexBoneIndices.w);

    vec4 skinnedPosition =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexPosition, 1.0));

    vec3 skinnedNormal =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexNormal, 0.0)).xyz +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexNormal, 0.0)).xyz +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexNormal, 0.0)).xyz +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexNormal, 0.0)).xyz;

    fragPosition = vec3(matModel*skinnedPosition);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(skinnedNormal, 0.0)));
    gl_Position = mvp*skinnedPosition;
}
)";
#else
constexpr const char* kLightingVertexShader = R"(#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 1.0)));
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kLightingFragmentShader = R"(#version 330

in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

out vec4 finalColor;

#define MAX_LIGHTS 4
#define LIGHT_POINT 1

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
};

uniform Light lights[MAX_LIGHTS];
uniform vec4 ambient;
uniform vec3 viewPos;

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (lights[i].enabled == 1 && lights[i].type == LIGHT_POINT)
        {
            vec3 light = normalize(lights[i].position - fragPosition);
            float NdotL = max(dot(normal, light), 0.0);
            lightDot += lights[i].color.rgb*NdotL;

            float specCo = 0.0;
            if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(light), normal))), 16.0);
            specular += specCo*lights[i].color.rgb;
        }
    }

    finalColor = texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0));
    finalColor += texelColor*(ambient/10.0)*colDiffuse;
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
)";

// Skinned variant of the lit vertex shader (dsl-model-animation D2): bone
// attributes skin position and normal; the fragment shader is shared.
// MAX_BONES must match kMaxSkinningBones.
constexpr const char* kSkinnedLightingVertexShader = R"(#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

#define MAX_BONES 128

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform mat4 boneMatrices[MAX_BONES];

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main()
{
    int boneIndex0 = int(vertexBoneIndices.x);
    int boneIndex1 = int(vertexBoneIndices.y);
    int boneIndex2 = int(vertexBoneIndices.z);
    int boneIndex3 = int(vertexBoneIndices.w);

    vec4 skinnedPosition =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexPosition, 1.0)) +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexPosition, 1.0));

    vec3 skinnedNormal =
        vertexBoneWeights.x*(boneMatrices[boneIndex0]*vec4(vertexNormal, 0.0)).xyz +
        vertexBoneWeights.y*(boneMatrices[boneIndex1]*vec4(vertexNormal, 0.0)).xyz +
        vertexBoneWeights.z*(boneMatrices[boneIndex2]*vec4(vertexNormal, 0.0)).xyz +
        vertexBoneWeights.w*(boneMatrices[boneIndex3]*vec4(vertexNormal, 0.0)).xyz;

    fragPosition = vec3(matModel*skinnedPosition);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal*vec4(skinnedNormal, 0.0)));
    gl_Position = mvp*skinnedPosition;
}
)";
#endif

struct LightingShaderState {
    Shader shader{};
    bool loaded{false};
    int ambient_loc{-1};
    int view_pos_loc{-1};
    std::array<int, kMaxMeshLights> enabled_locs{};
    std::array<int, kMaxMeshLights> type_locs{};
    std::array<int, kMaxMeshLights> position_locs{};
    std::array<int, kMaxMeshLights> target_locs{};
    std::array<int, kMaxMeshLights> color_locs{};
};

LightingShaderState& lighting_shader_state() noexcept {
    static LightingShaderState state{};
    return state;
}

// Skinned shader variant state (dsl-model-animation D2): the same lighting
// uniforms resolved against the skinned program, plus bone locations stored in
// shader.locs so raylib's DrawMesh binds the bone attribute VBOs.
LightingShaderState& skinned_lighting_shader_state() noexcept {
    static LightingShaderState state{};
    return state;
}

#ifndef CACTUS_RAYLIB_FAKE
std::string light_uniform_name(const int index, const std::string_view field) {
    std::string result{"lights["};
    result += std::to_string(index);
    result += "].";
    result += field;
    return result;
}
#endif

void reset_lighting_shader_state(LightingShaderState& state) noexcept {
    state = {};
    state.enabled_locs.fill(-1);
    state.type_locs.fill(-1);
    state.position_locs.fill(-1);
    state.target_locs.fill(-1);
    state.color_locs.fill(-1);
}

bool load_lighting_shader(LightingShaderState& state, const char* vertex_src, const char* fragment_src) {
#ifdef CACTUS_RAYLIB_FAKE
    // Shader compilation (LoadShaderFromMemory) and GetShaderLocation both
    // need a real GL context, which headless behavioral tests never create.
    // Every caller already treats "no shader" as a valid, gracefully
    // degrading outcome (see ensure_lighting_shader's nullptr contract), so
    // shaders simply never load under the fake, regardless of the fake's
    // scripted window_ready — that flag is a business-logic gate (should
    // resources be considered ready), not a real-context guarantee.
    (void)state;
    (void)vertex_src;
    (void)fragment_src;
    return false;
#else
    if (!cactus::runtime::raylib::IsWindowReady()) {
        return false;
    }

    reset_lighting_shader_state(state);
    state.shader = LoadShaderFromMemory(vertex_src, fragment_src);
    if (!IsShaderValid(state.shader)) {
        reset_lighting_shader_state(state);
        return false;
    }

    state.loaded                                = true;
    state.shader.locs[SHADER_LOC_MATRIX_MODEL]  = GetShaderLocation(state.shader, "matModel");
    state.shader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(state.shader, "matNormal");
    state.shader.locs[SHADER_LOC_VECTOR_VIEW]   = GetShaderLocation(state.shader, "viewPos");
    state.ambient_loc                           = GetShaderLocation(state.shader, "ambient");
    state.view_pos_loc                          = GetShaderLocation(state.shader, "viewPos");

    for (int i = 0; i < kMaxMeshLights; ++i) {
        const auto index    = static_cast<std::size_t>(i);
        const auto location = [&](const std::string_view field) {
            const std::string uniform = light_uniform_name(i, field);
            return GetShaderLocation(state.shader, uniform.c_str());
        };
        state.enabled_locs[index]  = location("enabled");
        state.type_locs[index]     = location("type");
        state.position_locs[index] = location("position");
        state.target_locs[index]   = location("target");
        state.color_locs[index]    = location("color");
    }

    return true;
#endif
}

Shader* ensure_lighting_shader() {
    auto& state = lighting_shader_state();
    if (state.loaded) {
        return &state.shader;
    }
    if (!load_lighting_shader(state, kLightingVertexShader, kLightingFragmentShader)) {
        return nullptr;
    }
    return &state.shader;
}

Shader* ensure_skinned_lighting_shader() {
    auto& state = skinned_lighting_shader_state();
    if (state.loaded) {
        return &state.shader;
    }
    if (!load_lighting_shader(state, kSkinnedLightingVertexShader, kLightingFragmentShader)) {
        return nullptr;
    }
    // Bone locations: the uniform-matrix upload reads BONETRANSFORMS, and
    // DrawMesh only binds the bone VBOs when the attrib locations are set.
    auto& shader                                    = state.shader;
    shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS]   = GetShaderLocation(shader, "boneMatrices");
    shader.locs[SHADER_LOC_VERTEX_BONEIDS]          = GetShaderLocationAttrib(shader, "vertexBoneIndices");
    shader.locs[SHADER_LOC_VERTEX_BONEWEIGHTS]      = GetShaderLocationAttrib(shader, "vertexBoneWeights");
    return &state.shader;
}

void clear_lighting_shader() noexcept {
    for (auto* state : {&lighting_shader_state(), &skinned_lighting_shader_state()}) {
        if (state->loaded && cactus::runtime::raylib::IsWindowReady()) {
            UnloadShader(state->shader);
        }
        reset_lighting_shader_state(*state);
    }
}

void apply_point_lights_to(const LightingShaderState& shader_state, const Camera3D& camera, const int active_lights) {
    const Shader& shader = shader_state.shader;
    const std::array<float, 4> ambient{0.22F, 0.22F, 0.28F, 1.0F};
    const std::array<float, 3> view_pos{camera.position.x, camera.position.y, camera.position.z};
    SetShaderValue(shader, shader_state.ambient_loc, ambient.data(), SHADER_UNIFORM_VEC4);
    SetShaderValue(shader, shader_state.view_pos_loc, view_pos.data(), SHADER_UNIFORM_VEC3);

    for (int i = 0; i < kMaxMeshLights; ++i) {
        const int enabled = i < active_lights ? 1 : 0;
        const int type    = kPointLightType;
        const auto zero   = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        std::array<float, 3> position{zero.x, zero.y, zero.z};
        std::array<float, 3> target{zero.x, zero.y, zero.z};
        std::array<float, 4> color{0.0F, 0.0F, 0.0F, 1.0F};

        if (i < active_lights) {
            const auto& light = point_light_queue()[static_cast<std::size_t>(i)];
            position[0]       = light.position.x;
            position[1]       = light.position.y;
            position[2]       = light.position.z;
            color[0]          = (static_cast<float>(light.color.r) / 255.0F) * light.intensity;
            color[1]          = (static_cast<float>(light.color.g) / 255.0F) * light.intensity;
            color[2]          = (static_cast<float>(light.color.b) / 255.0F) * light.intensity;
            color[3]          = static_cast<float>(light.color.a) / 255.0F;
        }

        const auto index = static_cast<std::size_t>(i);
        SetShaderValue(shader, shader_state.enabled_locs[index], &enabled, SHADER_UNIFORM_INT);
        SetShaderValue(shader, shader_state.type_locs[index], &type, SHADER_UNIFORM_INT);
        SetShaderValue(shader, shader_state.position_locs[index], position.data(), SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, shader_state.target_locs[index], target.data(), SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, shader_state.color_locs[index], color.data(), SHADER_UNIFORM_VEC4);
    }
}

void apply_point_lights(const Camera3D& camera) {
    const int active_lights = std::min(static_cast<int>(point_light_queue().size()), kMaxMeshLights);
    render_debug_state_storage().active_point_lights  = active_lights;
    render_debug_state_storage().used_lit_mesh_shader = active_lights > 0;

    // Both variants share the fragment lighting model, so both must see the
    // same light uniforms. The skinned variant is ensured here (not only at
    // model-bind time) so a skinned model loading mid-flush is lit the same
    // frame it first appears.
    if (ensure_lighting_shader() != nullptr) {
        apply_point_lights_to(lighting_shader_state(), camera, active_lights);
    }
    if (ensure_skinned_lighting_shader() != nullptr) {
        apply_point_lights_to(skinned_lighting_shader_state(), camera, active_lights);
    }
}

void note_missing_asset() noexcept {
    ++render_debug_state_storage().missing_assets;
}

Color placeholder_material_color(const std::string_view asset_id) noexcept {
    if (asset_id.contains("ground")) {
        return Color{.r = 76, .g = 154, .b = 74, .a = 255};
    }
    if (asset_id.contains("bomb") || asset_id.contains("orange")) {
        return Color{.r = 255, .g = 149, .b = 32, .a = 255};
    }
    if (asset_id.contains("trunk") || asset_id.contains("brown")) {
        return Color{.r = 129, .g = 82, .b = 45, .a = 255};
    }
    if (asset_id.contains("crown") || asset_id.contains("green")) {
        return Color{.r = 50, .g = 180, .b = 75, .a = 255};
    }
    if (asset_id.contains("player") || asset_id.contains("blue")) {
        return Color{.r = 66, .g = 139, .b = 255, .a = 255};
    }
    return WHITE;
}

}  // namespace

MeshPlaceholderShape placeholder_mesh_shape(const std::string_view asset_id) noexcept {
    if (asset_id.contains("sphere")) {
        return MeshPlaceholderShape::Sphere;
    }
    return MeshPlaceholderShape::Cube;
}

namespace {

void clear_texture_store() noexcept {
    for (auto& [runtime_id, entry] : textures()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && cactus::runtime::raylib::IsWindowReady()) {
            UnloadTexture(entry.texture);
        }
    }
    textures().clear();
}

void clear_mesh_store() noexcept {
    for (auto& [runtime_id, entry] : meshes()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && cactus::runtime::raylib::IsWindowReady()) {
            UnloadMesh(entry.mesh);
        }
    }
    meshes().clear();
}

void clear_material_store() noexcept {
    for (auto& [runtime_id, entry] : materials()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && cactus::runtime::raylib::IsWindowReady()) {
            UnloadMaterial(entry.material);
        }
    }
    materials().clear();
}

void clear_model_store() noexcept {
    for (auto& [runtime_id, entry] : models()) {
        (void)runtime_id;
        if (entry.animations != nullptr) {
            UnloadModelAnimations(entry.animations, entry.animation_count);
        }
        if (entry.loaded && cactus::runtime::raylib::IsWindowReady()) {
            UnloadModel(entry.model);
        }
    }
    models().clear();
    diagnosed_missing_model_handles().clear();
    diagnosed_invalid_clips().clear();
}

Texture2D* ensure_texture_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = textures()[runtime_id];
    if (!entry.loaded) {
        if (!cactus::runtime::raylib::IsWindowReady()) {
            return nullptr;
        }
#ifdef CACTUS_RAYLIB_FAKE
        // GenImageColor/LoadTextureFromImage/UnloadImage need a real GL context
        // to create GPU resources; headless behavioral tests never create one
        // (see ensure_mesh_resource's identical guard above). The fake's
        // DrawTexturePro only records its Texture2D argument by value, so a
        // zeroed placeholder is sufficient once the window is (fake-)ready.
        entry.texture = Texture2D{};
#else
        Image image   = GenImageColor(1, 1, WHITE);
        entry.texture = LoadTextureFromImage(image);
        UnloadImage(image);
#endif
        entry.loaded = true;
        entry.owned  = true;
    }
    return &entry.texture;
}

Mesh* ensure_mesh_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = meshes()[runtime_id];
    if (!entry.loaded) {
        if (!cactus::runtime::raylib::IsWindowReady()) {
            return nullptr;
        }
#ifdef CACTUS_RAYLIB_FAKE
        // UploadMesh needs a real GL context to create VBOs; headless
        // behavioral tests never create one. The fake's DrawMesh only
        // records its Mesh argument by value (never dereferences its data
        // pointers), so a zeroed placeholder is sufficient once the window
        // is (fake-)ready.
        entry.mesh = Mesh{};
#else
        // Both generators upload to the GPU internally; no separate
        // UploadMesh call is needed (or safe to skip re-triggering) here.
        entry.mesh = placeholder_mesh_shape(entry.path) == MeshPlaceholderShape::Sphere
                         ? GenMeshSphere(1.0F, kPlaceholderSphereRings, kPlaceholderSphereSlices)
                         : GenMeshCube(1.0F, 1.0F, 1.0F);
#endif
        entry.loaded = true;
        entry.owned  = true;
    }
    return &entry.mesh;
}

Material* ensure_material_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = materials()[runtime_id];
    if (!entry.loaded) {
        if (!cactus::runtime::raylib::IsWindowReady()) {
            return nullptr;
        }
        entry.material = LoadMaterialDefault();
        entry.loaded   = true;
        entry.owned    = true;
    }
    if (Shader* shader = ensure_lighting_shader(); shader != nullptr) {
        entry.material.shader = *shader;
    }
    return &entry.material;
}

// Record a diagnostic for a model load failure. Callers guarantee this runs at
// most once per asset (the entry's `failed` or `loaded` flag flips exactly once).
void record_model_load_failure(const std::string& message) {
    render_debug_state_storage().model_diagnostics.push_back(message);
    note_missing_asset();
}

#ifdef CACTUS_RAYLIB_FAKE
// LoadModel parses the GLB *and* uploads its meshes/textures to the GPU;
// headless behavioral tests never create a real GL context. Every fake
// model shares one static, unskinned, single-submesh placeholder — the
// fake's DrawMesh only records its Mesh/Material arguments by value, and
// per-entity attribution comes from the caller-supplied transform, not from
// mesh/material content, so sharing is safe. boneMatrices stays null so
// upload_skinned_pose (which needs a real GL context too) no-ops.
Model fake_model_resource() noexcept {
    static Mesh mesh{};
    static MaterialMap material_maps[1]{};
    static Material material{.shader = {}, .maps = material_maps, .params = {}};
    static int mesh_material[1]{0};

    Model model{};
    model.transform     = MatrixIdentity();
    model.meshCount     = 1;
    model.materialCount = 1;
    model.meshes        = &mesh;
    model.materials     = &material;
    model.meshMaterial  = mesh_material;
    return model;
}
#endif

// Lazily load a registered model on first render-time use (dsl-model-assets
// D3/D4/D6). Missing or empty files mark the entry failed — the draw is
// skipped without placeholder geometry and the load is never retried.
Model* ensure_model_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    const auto it = models().find(runtime_id);
    if (it == models().end()) {
        return nullptr;
    }
    auto& entry = it->second;
    if (entry.failed) {
        return nullptr;
    }
    if (entry.loaded) {
        return &entry.model;
    }
    if (entry.path.empty() || !FileExists(entry.path.c_str())) {
        entry.failed = true;
        record_model_load_failure("model file missing: " + entry.path);
        return nullptr;
    }
    if (!cactus::runtime::raylib::IsWindowReady()) {
        return nullptr;
    }
#ifdef CACTUS_RAYLIB_FAKE
    entry.model = fake_model_resource();
#else
    entry.model = LoadModel(entry.path.c_str());
#endif
    if (entry.model.meshCount == 0) {
        entry.failed = true;
        record_model_load_failure("model load failed: " + entry.path);
        return nullptr;
    }
    // Bind every embedded material to the lit shader — the skinned variant
    // when the model has a skeleton within the bone limit, the static variant
    // otherwise (dsl-model-animation D2). raylib already mapped each
    // material's baseColorFactor into the albedo map color, which the lit
    // shader multiplies through (D6).
    const int bone_count = entry.model.skeleton.boneCount;
    if (bone_count > kMaxSkinningBones) {
        record_model_load_failure("model exceeds " + std::to_string(kMaxSkinningBones) +
                                  "-bone skinning limit, rendering unskinned: " + entry.path);
    }
    entry.skinned  = bone_count > 0 && bone_count <= kMaxSkinningBones;
    Shader* shader = entry.skinned ? ensure_skinned_lighting_shader() : ensure_lighting_shader();
    if (entry.skinned && shader == nullptr) {
        entry.skinned = false;
        shader        = ensure_lighting_shader();
    }
    if (shader != nullptr) {
        for (int i = 0; i < entry.model.materialCount; ++i) {
            entry.model.materials[i].shader = *shader;
        }
    }
    entry.loaded = true;
    return &entry.model;
}

// Lazily load clip data beside the model (dsl-model-animation D4). The load is
// attempted once; a missing file or clip-less model leaves the count at zero.
void ensure_model_animations(ModelResourceEntry& entry) {
    if (entry.animations_load_attempted) {
        return;
    }
    entry.animations_load_attempted = true;
    if (entry.path.empty() || !FileExists(entry.path.c_str())) {
        return;
    }
#ifdef CACTUS_RAYLIB_FAKE
    // Fake models (see fake_model_resource) never carry animation clips —
    // resolve_animation_clip already degrades gracefully (bind pose, one
    // diagnostic) when animation_count stays 0, and clip playback isn't
    // part of what headless behavioral tests assert.
    return;
#else
    entry.animations = LoadModelAnimations(entry.path.c_str(), &entry.animation_count);
    if (entry.animations == nullptr) {
        entry.animation_count = 0;
    }
#endif
}

// Resolve an animated submission's clip to animation data. Out-of-range or
// clip-less state degrades to bind pose (nullptr) with at most one diagnostic
// per (asset, clip) pair (dsl-model-animation D3).
const ModelAnimation* resolve_animation_clip(ModelResourceEntry& entry, const int runtime_id, const int clip) {
    ensure_model_animations(entry);
    if (clip >= 0 && clip < entry.animation_count) {
        return &entry.animations[clip];
    }
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(runtime_id)) << 32U) |
                              static_cast<std::uint32_t>(clip);
    if (diagnosed_invalid_clips().insert(key).second) {
        render_debug_state_storage().model_diagnostics.push_back(
            "invalid animation clip " + std::to_string(clip) + " (model has " +
            std::to_string(entry.animation_count) + "): " + entry.path);
    }
    return nullptr;
}

// Re-pose the shared scratch model for one submission and upload its bone
// matrices to the skinned shader, mirroring DrawModelEx's upload (rmodels.c) —
// DrawMesh alone does not upload them. The pose buffer is shared across every
// entity using this model, so the upload must stay immediately before the
// submission's draws; do not reorder across submissions (D1). A null clip
// renders the bind pose.
void upload_skinned_pose(Model& model, const ModelAnimation* clip_anim, const float time) {
    if (model.boneMatrices == nullptr) {
        return;
    }
    if (clip_anim != nullptr && clip_anim->keyframeCount > 0) {
        // Wrap defensively: authored time is wrapped by the animation rule,
        // but time is writable and negative frames would index out of bounds
        // inside UpdateModelAnimation.
        const auto frame_count = static_cast<float>(clip_anim->keyframeCount);
        float frame            = std::fmod(time * kGltfKeyframesPerSecond, frame_count);
        if (frame < 0.0F) {
            frame += frame_count;
        }
        UpdateModelAnimation(model, *clip_anim, frame);
    } else {
        // Bind pose: identity bone matrices (an earlier submission may have
        // left another entity's pose in the scratch buffer).
        for (int b = 0; b < model.skeleton.boneCount; ++b) {
            model.boneMatrices[b] = MatrixIdentity();
        }
    }
    if (Shader* skinned = ensure_skinned_lighting_shader(); skinned != nullptr) {
        rlEnableShader(skinned->id);
        rlSetUniformMatrices(skinned->locs[SHADER_LOC_MATRIX_BONETRANSFORMS], model.boneMatrices,
                             model.skeleton.boneCount);
    }
}

Matrix mesh_transform_matrix(const MeshSubmission& submission) noexcept {
    const Matrix scale       = MatrixScale(submission.scale.x, submission.scale.y, submission.scale.z);
    const Matrix rotation    = QuaternionToMatrix(submission.rotation);
    const Matrix translation = MatrixTranslate(submission.position.x, submission.position.y, submission.position.z);
    return MatrixMultiply(MatrixMultiply(scale, rotation), translation);
}

void flush_mesh_queue() noexcept {
    if (mesh_queue().empty()) {
        return;
    }

    render_debug_state_storage().used_default_3d_camera = true;
    render_debug_state_storage().active_point_lights =
        std::min(static_cast<int>(point_light_queue().size()), kMaxMeshLights);
    render_debug_state_storage().used_lit_mesh_shader = render_debug_state_storage().active_point_lights > 0;
    if (!cactus::runtime::raylib::IsWindowReady()) {
        return;
    }

    const Camera3D camera = get_active_camera_3d();

    apply_point_lights(camera);

    cactus::runtime::raylib::BeginMode3D(camera);
    for (const auto& submission : mesh_queue()) {
        Mesh* mesh         = ensure_mesh_resource(submission.mesh_runtime_id);
        Material* material = ensure_material_resource(submission.material_runtime_id);
        if (mesh == nullptr || material == nullptr) {
            continue;
        }
        material->maps[MATERIAL_MAP_DIFFUSE].color = submission.diffuse_color;
        cactus::runtime::raylib::DrawMesh(*mesh, *material, mesh_transform_matrix(submission));
    }
    cactus::runtime::raylib::EndMode3D();
}

void flush_model_queue() noexcept {
    if (model_queue().empty()) {
        return;
    }

    render_debug_state_storage().used_default_3d_camera = true;
    render_debug_state_storage().active_point_lights =
        std::min(static_cast<int>(point_light_queue().size()), kMaxMeshLights);
    render_debug_state_storage().used_lit_mesh_shader = render_debug_state_storage().active_point_lights > 0;

    // Materialization still runs headless so failure bookkeeping stays
    // test-observable; only the draw calls require a window.
    const bool window_ready = cactus::runtime::raylib::IsWindowReady();
    if (window_ready) {
        const Camera3D camera = get_active_camera_3d();
        apply_point_lights(camera);
        cactus::runtime::raylib::BeginMode3D(camera);
    }
    for (const auto& submission : model_queue()) {
        const auto it = models().find(submission.model_runtime_id);
        ModelResourceEntry* entry = it != models().end() ? &it->second : nullptr;

        // Clip validation runs headless too, so invalid-clip diagnostics stay
        // test-observable (D3).
        const ModelAnimation* clip_anim = nullptr;
        if (submission.animated && entry != nullptr && !entry->failed) {
            clip_anim = resolve_animation_clip(*entry, submission.model_runtime_id, submission.clip);
        }

        // Trigger lazy load only when the entry exists and hasn't failed; avoids
        // a redundant second map lookup on already-loaded models each frame.
        Model* model = (entry != nullptr && !entry->failed)
                           ? ensure_model_resource(submission.model_runtime_id)
                           : nullptr;

        if (model == nullptr || !window_ready) {
            continue;
        }
        const Matrix xform = mesh_transform_matrix(MeshSubmission{
            .position = submission.position,
            .rotation = submission.rotation,
            .scale    = submission.scale,
        });
        if (entry->skinned) {
            upload_skinned_pose(*model, clip_anim, submission.time);
        }
        // Draw every submesh with the embedded material bound to it in the file.
        for (int i = 0; i < model->meshCount; ++i) {
            const int material_index = model->meshMaterial[i];
            cactus::runtime::raylib::DrawMesh(model->meshes[i], model->materials[material_index], xform);
        }
        ++render_debug_state_storage().drawn_models;
    }
    if (window_ready) {
        cactus::runtime::raylib::EndMode3D();
    }
}

// Shared 1×1 XY-plane mesh (normal = +Z). V coordinates are pre-flipped to
// cancel the RenderTexture2D vertical inversion so text appears upright.
Mesh& text_plane_mesh() noexcept {
    static Mesh mesh{};
    static bool ready{false};
    if (!ready && cactus::runtime::raylib::IsWindowReady()) {
        constexpr int kVerts     = 4;
        constexpr int kTriangles = 2;

        mesh.vertexCount   = kVerts;
        mesh.triangleCount = kTriangles;

        mesh.vertices  = static_cast<float*>(MemAlloc(static_cast<unsigned>(kVerts * 3) * sizeof(float)));
        mesh.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned>(kVerts * 2) * sizeof(float)));
        mesh.normals   = static_cast<float*>(MemAlloc(static_cast<unsigned>(kVerts * 3) * sizeof(float)));
        mesh.indices =
            static_cast<unsigned short*>(MemAlloc(static_cast<unsigned>(kTriangles * 3) * sizeof(unsigned short)));

        // Positions: XY plane, centred, counter-clockwise when viewed from +Z
        // [0] bottom-left  [1] bottom-right  [2] top-right  [3] top-left
        const std::array<float, 12> verts = {
            -0.5F,
            -0.5F,
            0.0F,
            0.5F,
            -0.5F,
            0.0F,
            0.5F,
            0.5F,
            0.0F,
            -0.5F,
            0.5F,
            0.0F,
        };
        // UVs: V=0 at top, V=1 at bottom — cancels RenderTexture2D Y-flip
        const std::array<float, 8> uvs = {
            0.0F,
            1.0F,
            1.0F,
            1.0F,
            1.0F,
            0.0F,
            0.0F,
            0.0F,
        };
        const std::array<float, 12> normals = {
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            0.0F,
            1.0F,
        };
        const std::array<unsigned short, 6> indices = {0, 1, 2, 0, 2, 3};

        for (int i = 0; i < kVerts * 3; ++i) {
            mesh.vertices[i] = verts[static_cast<size_t>(i)];
        }
        for (int i = 0; i < kVerts * 2; ++i) {
            mesh.texcoords[i] = uvs[static_cast<size_t>(i)];
        }
        for (int i = 0; i < kVerts * 3; ++i) {
            mesh.normals[i] = normals[static_cast<size_t>(i)];
        }
        for (int i = 0; i < kTriangles * 3; ++i) {
            mesh.indices[i] = indices[static_cast<size_t>(i)];
        }

        UploadMesh(&mesh, false);
        ready = true;
    }
    return mesh;
}

void flush_text_2d_queue() noexcept {
    if (text_2d_queue().empty() || !cactus::runtime::raylib::IsWindowReady()) {
        return;
    }
    const Font font          = GetFontDefault();
    constexpr float kSpacing = 1.0F;

    Camera2D camera{};
    camera.zoom = 1.0F;
    cactus::runtime::raylib::BeginMode2D(camera);
    for (const auto& sub : text_2d_queue()) {
        const auto fs      = static_cast<float>(sub.font_size);
        const Vector2 size = MeasureTextEx(font, sub.text.c_str(), fs, kSpacing);
        const Vector2 origin{.x = size.x * 0.5F, .y = size.y * 0.5F};
        cactus::runtime::raylib::DrawTextPro(font, sub.text.c_str(), sub.position, origin, sub.rotation_deg, fs, kSpacing, sub.color);
    }
    cactus::runtime::raylib::EndMode2D();
}

void flush_screen_label_queue() noexcept {
    if (screen_label_queue().empty() || !cactus::runtime::raylib::IsWindowReady()) {
        return;
    }
    const Font font          = GetFontDefault();
    constexpr float kSpacing = 1.0F;
    for (const auto& sub : screen_label_queue()) {
        cactus::runtime::raylib::DrawTextEx(font, sub.text.c_str(), sub.position, static_cast<float>(sub.font_size), kSpacing, sub.color);
    }
}

void flush_text_3d_queue() noexcept {
    if (text_3d_queue().empty() || !cactus::runtime::raylib::IsWindowReady()) {
        return;
    }

    constexpr int kPixelsPerWorldUnit = 256;

    // Phase 1: bake dirty render textures (must be outside any BeginMode block)
    for (auto& sub : text_3d_queue()) {
        if (sub.text.empty()) {
            continue;
        }
        auto& entry      = text_label_3d_cache()[sub.entity_id];
        const bool dirty = !entry.loaded || entry.cached_text != sub.text || entry.cached_font_size != sub.font_size ||
                           entry.cached_color.r != sub.color.r || entry.cached_color.g != sub.color.g ||
                           entry.cached_color.b != sub.color.b || entry.cached_color.a != sub.color.a;
        if (!dirty) {
            continue;
        }
        const int tex_h = std::max(64, sub.font_size * kPixelsPerWorldUnit);
        const int tex_w = tex_h * 4;
        if (entry.loaded) {
            UnloadRenderTexture(entry.rt);
        }
        entry.rt     = LoadRenderTexture(tex_w, tex_h);
        entry.loaded = true;

        const int bake_fs   = static_cast<int>(static_cast<float>(tex_h) * 0.75F);
        const int text_w_px = MeasureText(sub.text.c_str(), bake_fs);
        const int draw_x    = (tex_w - text_w_px) / 2;
        const int draw_y    = (tex_h - bake_fs) / 2;

        cactus::runtime::raylib::BeginTextureMode(entry.rt);
        cactus::runtime::raylib::ClearBackground(Color{.r = 0, .g = 0, .b = 0, .a = 0});
        cactus::runtime::raylib::DrawText(sub.text.c_str(), draw_x, draw_y, bake_fs, sub.color);
        cactus::runtime::raylib::EndTextureMode();

        entry.cached_text      = sub.text;
        entry.cached_font_size = sub.font_size;
        entry.cached_color     = sub.color;
    }

    // Phase 2: draw plane meshes inside the 3D camera block
    const Camera3D camera = get_active_camera_3d();

    Mesh& plane = text_plane_mesh();

    Material mat = LoadMaterialDefault();
    cactus::runtime::raylib::BeginMode3D(camera);
    for (const auto& sub : text_3d_queue()) {
        if (sub.text.empty()) {
            continue;
        }
        auto it = text_label_3d_cache().find(sub.entity_id);
        if (it == text_label_3d_cache().end() || !it->second.loaded) {
            continue;
        }
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = it->second.rt.texture;

        const Matrix xform = mesh_transform_matrix(MeshSubmission{
            .position = sub.position,
            .rotation = sub.rotation,
            .scale    = sub.scale,
        });
        cactus::runtime::raylib::DrawMesh(plane, mat, xform);
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    }
    cactus::runtime::raylib::EndMode3D();
    UnloadMaterial(mat);
}

void flush_sprite_queue() noexcept {
    if (sprite_queue().empty()) {
        return;
    }

    std::ranges::stable_sort(sprite_queue(), [](const auto& lhs, const auto& rhs) { return lhs.layer < rhs.layer; });
    render_debug_state_storage().used_default_2d_camera = true;
    render_debug_state_storage().drawn_sprite_layers.clear();
    for (const auto& submission : sprite_queue()) {
        render_debug_state_storage().drawn_sprite_layers.push_back(submission.layer);
    }

    if (!cactus::runtime::raylib::IsWindowReady()) {
        return;
    }

    Camera2D camera{};
    camera.offset   = Vector2{.x = 0.0F, .y = 0.0F};
    camera.target   = Vector2{.x = 0.0F, .y = 0.0F};
    camera.rotation = 0.0F;
    camera.zoom     = 1.0F;

    cactus::runtime::raylib::BeginMode2D(camera);
    for (const auto& submission : sprite_queue()) {
        Texture2D* texture = ensure_texture_resource(submission.runtime_id);
        if (texture == nullptr) {
            continue;
        }
        const Rectangle src{.x      = 0.0F,
                            .y      = 0.0F,
                            .width  = static_cast<float>(texture->width),
                            .height = static_cast<float>(texture->height)};
        const Rectangle dst{.x      = submission.position.x,
                            .y      = submission.position.y,
                            .width  = submission.size.x,
                            .height = submission.size.y};
        cactus::runtime::raylib::DrawTexturePro(*texture, src, dst, Vector2{.x = 0.0F, .y = 0.0F}, 0.0F, submission.color);
    }
    cactus::runtime::raylib::EndMode2D();
}

void submit_model_impl(const Vector3 position,
                       const Quat rotation,
                       const Vector3 scale,
                       const AssetHandle model,
                       const bool visible,
                       const bool cast_shadow,
                       const bool animated,
                       const int clip,
                       const float time) noexcept {
    if (!visible) {
        return;
    }
    const auto resolved = shared_asset_registry().resolve(AssetKind::Model, model);
    if (!resolved.valid()) {
        note_missing_asset();
        if (diagnosed_missing_model_handles().insert(model).second) {
            render_debug_state_storage().model_diagnostics.push_back("missing model asset handle " +
                                                                     std::to_string(model));
        }
        return;
    }
    auto& entry = models()[resolved.runtime_id];
    if (entry.path.empty()) {
        entry.path = std::string{resolved.asset_id};
    }
    model_queue().push_back(ModelSubmission{
        .position         = position,
        .rotation         = rotation,
        .scale            = scale,
        .model_runtime_id = resolved.runtime_id,
        .cast_shadow      = cast_shadow,
        .clip             = clip,
        .time             = time,
        .animated         = animated,
    });
    ++render_debug_state_storage().submitted_models;
    if (animated) {
        render_debug_state_storage().animated_model_submissions.push_back({.clip = clip, .time = time});
    }
}
}  // namespace

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

void reset_render_debug_state() noexcept {
    render_debug_state_storage() = {};
    sprite_queue().clear();
    mesh_queue().clear();
    model_queue().clear();
    point_light_queue().clear();
    text_2d_queue().clear();
    text_3d_queue().clear();
    screen_label_queue().clear();
    if (cactus::runtime::raylib::IsWindowReady()) {
        for (auto& [id, entry] : text_label_3d_cache()) {
            if (entry.loaded) {
                UnloadRenderTexture(entry.rt);
            }
        }
    }
    text_label_3d_cache().clear();
    clear_texture_store();
    clear_mesh_store();
    clear_material_store();
    clear_model_store();
    clear_lighting_shader();
    shared_asset_registry().clear_diagnostics();
}

const RenderDebugState& render_debug_state() noexcept {
    return render_debug_state_storage();
}

void begin_render_frame() noexcept {
    sprite_queue().clear();
    mesh_queue().clear();
    model_queue().clear();
    point_light_queue().clear();
    text_2d_queue().clear();
    text_3d_queue().clear();
    screen_label_queue().clear();
    render_debug_state_storage().used_default_2d_camera = false;
    render_debug_state_storage().used_default_3d_camera = false;
    render_debug_state_storage().active_point_lights    = 0;
    render_debug_state_storage().used_lit_mesh_shader   = false;
    render_debug_state_storage().drawn_sprite_layers.clear();
    render_debug_state_storage().submitted_mesh_colors.clear();
    render_debug_state_storage().animated_model_submissions.clear();
}

static void flush_viewport_queues() noexcept {
    flush_mesh_queue();
    flush_model_queue();
    flush_text_3d_queue();
    flush_sprite_queue();
    flush_text_2d_queue();
    mesh_queue().clear();
    model_queue().clear();
    text_3d_queue().clear();
    sprite_queue().clear();
    point_light_queue().clear();
    text_2d_queue().clear();
}

void end_render_frame() noexcept {
    flush_viewport_queues();
    // Screen labels are window-global: drawn last, after all viewport/world
    // rendering, so they overlay every view (never flushed per viewport).
    flush_screen_label_queue();
    screen_label_queue().clear();
}

void flush_viewport_3d() noexcept {
    flush_viewport_queues();
}

void submit_sprite(const Vector2 position,
                   const Vector2 size,
                   const Color color,
                   const AssetHandle texture,
                   const bool visible,
                   const int layer) noexcept {
    if (!visible) {
        return;
    }
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.ready()) {
        note_missing_asset();
        return;
    }
    sprite_queue().push_back(SpriteSubmission{
        .position   = position,
        .size       = size,
        .color      = color,
        .runtime_id = resolved.runtime_id,
        .layer      = layer,
    });
    ++render_debug_state_storage().submitted_sprites;
}

void advance_animated_sprite(const AssetHandle texture,
                             int& frame,
                             const int frame_count,
                             const float fps,
                             const bool playing,
                             const float dt) noexcept {
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.valid()) {
        note_missing_asset();
        return;
    }
    if (!playing || frame_count <= 0 || fps <= 0.0F) {
        return;
    }
    const auto step = static_cast<int>(dt * fps);
    if (step <= 0) {
        return;
    }
    frame = (frame + step) % frame_count;
    ++render_debug_state_storage().advanced_animated_sprites;
}

void submit_mesh(const Vector3 position,
                 const Quat rotation,
                 const Vector3 scale,
                 const AssetHandle mesh,
                 const AssetHandle material,
                 const bool visible,
                 const bool /*cast_shadow*/,
                 const Color tint) noexcept {
    if (!visible) {
        return;
    }
    const auto mesh_resolved     = shared_asset_registry().resolve(AssetKind::Mesh, mesh);
    const auto material_resolved = shared_asset_registry().resolve(AssetKind::Material, material);
    if (!mesh_resolved.ready() || !material_resolved.ready()) {
        note_missing_asset();
        return;
    }
    auto& mesh_entry = meshes()[mesh_resolved.runtime_id];
    if (mesh_entry.path.empty()) {
        mesh_entry.path = std::string{mesh_resolved.asset_id};
    }
    auto& mat_entry = materials()[material_resolved.runtime_id];
    if (!mat_entry.diffuse_color_set) {
        mat_entry.diffuse_color     = placeholder_material_color(material_resolved.asset_id);
        mat_entry.diffuse_color_set = true;
    }
    // Renderer.color is a per-entity multiplicative tint on the shared
    // material's resolved placeholder color (design.md: mirrors
    // BillboardRenderer.color's existing "#FFFFFFFF = no tint" contract), so
    // it's applied here per-submission rather than cached on mat_entry.
    const Color resolved_color = ColorTint(mat_entry.diffuse_color, tint);
    mesh_queue().push_back(MeshSubmission{
        .position            = position,
        .rotation            = rotation,
        .scale               = scale,
        .mesh_runtime_id     = mesh_resolved.runtime_id,
        .material_runtime_id = material_resolved.runtime_id,
        .diffuse_color       = resolved_color,
    });
    render_debug_state_storage().submitted_mesh_colors.push_back(resolved_color);
    ++render_debug_state_storage().submitted_meshes;
}

void submit_model(const Vector3 position,
                  const Quat rotation,
                  const Vector3 scale,
                  const AssetHandle model,
                  const bool visible,
                  const bool cast_shadow) noexcept {
    submit_model_impl(position, rotation, scale, model, visible, cast_shadow, false, 0, 0.0F);
}

void submit_model(const Vector3 position,
                  const Quat rotation,
                  const Vector3 scale,
                  const AssetHandle model,
                  const bool visible,
                  const bool cast_shadow,
                  const int clip,
                  const float time) noexcept {
    submit_model_impl(position, rotation, scale, model, visible, cast_shadow, true, clip, time);
}

// Resolve a model handle, fill entry.path if needed, and trigger the lazy
// load (D4: introspection works before first draw). Returns {nullptr, nullptr}
// if the handle is invalid.
struct ModelEntryAccess {
    ModelResourceEntry* entry;
    Model*              loaded;
};

static ModelEntryAccess prepare_model_entry(const AssetHandle model) noexcept {
    const auto resolved = shared_asset_registry().resolve(AssetKind::Model, model);
    if (!resolved.valid()) {
        return {.entry = nullptr, .loaded = nullptr};
    }
    auto& entry = models()[resolved.runtime_id];
    if (entry.path.empty()) {
        entry.path = std::string{resolved.asset_id};
    }
    return {.entry = &entry, .loaded = ensure_model_resource(resolved.runtime_id)};
}

int model_animation_count(const AssetHandle model) noexcept {
    auto [entry, loaded] = prepare_model_entry(model);
    if (entry == nullptr) { return 0; }
    ensure_model_animations(*entry);
    return entry->animation_count;
}

std::string model_animation_name(const AssetHandle model, const int clip) noexcept {
    auto [entry, loaded] = prepare_model_entry(model);
    if (entry == nullptr) { return ""; }
    ensure_model_animations(*entry);
    if (clip < 0 || clip >= entry->animation_count) { return ""; }
    const auto& anim = entry->animations[clip];
    return std::string{anim.name, strnlen(anim.name, sizeof(anim.name))};
}

float model_animation_duration(const AssetHandle model, const int clip) noexcept {
    auto [entry, loaded] = prepare_model_entry(model);
    if (entry == nullptr) { return 0.0F; }
    ensure_model_animations(*entry);
    if (clip < 0 || clip >= entry->animation_count) { return 0.0F; }
    return static_cast<float>(entry->animations[clip].keyframeCount) / kGltfKeyframesPerSecond;
}

Vector3 model_bounds_size(const AssetHandle model) noexcept {
    constexpr Vector3 kZeroExtents{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    auto [entry, loaded] = prepare_model_entry(model);
    if (loaded == nullptr) { return kZeroExtents; }
    const BoundingBox box = GetModelBoundingBox(*loaded);
    return Vector3{.x = box.max.x - box.min.x, .y = box.max.y - box.min.y, .z = box.max.z - box.min.z};
}

BoundingBox model_bounds_box(const AssetHandle model) noexcept {
    constexpr BoundingBox kZeroBox{.min = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                   .max = {.x = 0.0F, .y = 0.0F, .z = 0.0F}};
    auto [entry, loaded] = prepare_model_entry(model);
    if (loaded == nullptr) { return kZeroBox; }
    return GetModelBoundingBox(*loaded);
}

Vector3 model_bounds_center(const AssetHandle model) noexcept {
    const BoundingBox box = model_bounds_box(model);
    return Vector3{.x = (box.min.x + box.max.x) * 0.5F,
                   .y = (box.min.y + box.max.y) * 0.5F,
                   .z = (box.min.z + box.max.z) * 0.5F};
}

void submit_billboard(const Vector3 /*position*/,
                      const Vector2 /*size*/,
                      const Color /*color*/,
                      const AssetHandle texture,
                      const bool visible) noexcept {
    if (!visible) {
        return;
    }
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.ready()) {
        note_missing_asset();
        return;
    }
    ++render_debug_state_storage().submitted_billboards;
}

void register_point_light(const Vector3 position,
                          const Color color,
                          const float intensity,
                          const float range,
                          const bool enabled) noexcept {
    if (enabled) {
        point_light_queue().push_back(PointLightSubmission{
            .position  = position,
            .color     = color,
            .intensity = intensity,
            .range     = range,
        });
        ++render_debug_state_storage().registered_point_lights;
    }
}

void register_directional_light(const Vector3 /*direction*/,
                                const Color /*color*/,
                                const float /*intensity*/,
                                const bool enabled) noexcept {
    if (enabled) {
        ++render_debug_state_storage().registered_directional_lights;
    }
}

void submit_text_2d(const Vector2 position,
                    const float rotation_rad,
                    const int font_size,
                    const Color color,
                    const std::string& text,
                    const bool visible) noexcept {
    if (!visible) {
        return;
    }
    constexpr float kRadToDeg = 180.0F / std::numbers::pi_v<float>;
    text_2d_queue().push_back(TextSubmission2D{
        .position     = position,
        .rotation_deg = rotation_rad * kRadToDeg,
        .font_size    = font_size,
        .color        = color,
        .text         = text,
    });
}

void submit_screen_label(const Vector2 position,
                         const int font_size,
                         const Color color,
                         const std::string& text,
                         const bool visible) noexcept {
    if (!visible) {
        return;
    }
    screen_label_queue().push_back(ScreenLabelSubmission{
        .position  = position,
        .font_size = font_size,
        .color     = color,
        .text      = text,
    });
    ++render_debug_state_storage().submitted_screen_labels;
}

void submit_text_3d(const uint32_t entity_id,
                    const Vector3 position,
                    const Quat rotation,
                    const Vector3 scale,
                    const int font_size,
                    const Color color,
                    const std::string& text,
                    const bool visible) noexcept {
    if (!visible) {
        return;
    }
    text_3d_queue().push_back(TextSubmission3D{
        .entity_id = entity_id,
        .position  = position,
        .rotation  = rotation,
        .scale     = scale,
        .font_size = font_size,
        .color     = color,
        .text      = text,
    });
}

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent) {
    std::pmr::monotonic_buffer_resource scratch_resource;
    std::pmr::unordered_set<entt::entity> active_entities{&scratch_resource};

    std::function<void(entt::entity)> resolve;
    resolve = [&](entt::entity entity) -> void {
        if (!registry.valid(entity) || active_entities.contains(entity) || !has_local_world(entity)) {
            return;
        }

        active_entities.insert(entity);
        bool copied_local         = false;
        const entt::entity parent = get_parent(entity);
        if (parent != entt::null && registry.valid(parent) && has_local_world(parent)) {
            resolve(parent);
            accumulate_from_parent(parent, entity);
            copied_local = true;
        }
        if (!copied_local) {
            copy_local(entity);
        }
        active_entities.erase(entity);
    };

    auto& storage = registry.storage<entt::entity>();
    for (const auto entry : storage.each()) {
        resolve(std::get<0>(entry));
    }
}

void destroy_entity_recursive(
    entt::registry& registry,
    entt::entity entity,
    const std::function<void(entt::entity, const std::function<void(entt::entity)>&)>& visit_children) {
    static std::pmr::unsynchronized_pool_resource destroying_resource;
    static std::pmr::unordered_set<entt::entity> destroying_entities{&destroying_resource};
    if (!registry.valid(entity) || destroying_entities.contains(entity)) {
        return;
    }

    destroying_entities.insert(entity);
    std::pmr::monotonic_buffer_resource child_resource;
    std::pmr::vector<entt::entity> child_entities{&child_resource};
    visit_children(entity, [&](entt::entity child) { child_entities.push_back(child); });
    for (const auto child : child_entities) {
        destroy_entity_recursive(registry, child, visit_children);
    }
    if (registry.valid(entity)) {
        registry.destroy(entity);
    }
    destroying_entities.erase(entity);
}

// ── Sweep-and-prune broad phase (spatial-broadphase-runtime capability) ────────

namespace {

// Dimension-agnostic proxy shape shared by the 2D and 3D sweeps below, so the
// axis-selection/overlap/candidate-generation logic is written once.
template <std::size_t N>
struct SapInternalProxy {
    std::array<float, N> center{};
    float radius{0.0F};
};

template <std::size_t N>
[[nodiscard]] int sap_select_primary_axis(const std::vector<SapInternalProxy<N>>& proxies) noexcept {
    std::array<float, N> lo{};
    std::array<float, N> hi{};
    lo.fill(std::numeric_limits<float>::max());
    hi.fill(std::numeric_limits<float>::lowest());
    for (const auto& proxy : proxies) {
        for (std::size_t axis = 0; axis < N; ++axis) {
            lo[axis] = std::min(lo[axis], proxy.center[axis]);
            hi[axis] = std::max(hi[axis], proxy.center[axis]);
        }
    }
    int best_axis      = 0;
    float best_spread  = hi[0] - lo[0];
    for (std::size_t axis = 1; axis < N; ++axis) {
        const float spread = hi[axis] - lo[axis];
        if (spread > best_spread) {
            best_spread = spread;
            best_axis   = static_cast<int>(axis);
        }
    }
    return best_axis;
}

template <std::size_t N>
[[nodiscard]] bool sap_aabb_overlap(const SapInternalProxy<N>& lhs, const SapInternalProxy<N>& rhs) noexcept {
    for (std::size_t axis = 0; axis < N; ++axis) {
        const float lhs_lo = lhs.center[axis] - lhs.radius;
        const float lhs_hi = lhs.center[axis] + lhs.radius;
        const float rhs_lo = rhs.center[axis] - rhs.radius;
        const float rhs_hi = rhs.center[axis] + rhs.radius;
        if (lhs_hi < rhs_lo || rhs_hi < lhs_lo) {
            return false;
        }
    }
    return true;
}

// Every candidate pair below is verified with the same sap_aabb_overlap
// predicate regardless of which strategy found it, so brute force and the
// swept sweep necessarily agree on the candidate set for the same proxies.

template <std::size_t N>
[[nodiscard]] std::vector<SapCandidatePair> sap_candidates_brute_force(
    const std::vector<SapInternalProxy<N>>& proxies) {
    std::vector<SapCandidatePair> result;
    for (std::size_t i = 0; i < proxies.size(); ++i) {
        for (std::size_t j = i + 1; j < proxies.size(); ++j) {
            if (sap_aabb_overlap(proxies[i], proxies[j])) {
                result.push_back(SapCandidatePair{.first = i, .second = j});
            }
        }
    }
    return result;
}

template <std::size_t N>
[[nodiscard]] std::vector<SapCandidatePair> sap_candidates_swept(const std::vector<SapInternalProxy<N>>& proxies,
                                                                  int axis) {
    const auto axis_index = static_cast<std::size_t>(axis);
    std::vector<std::size_t> order(proxies.size());
    std::ranges::iota(order, std::size_t{0});
    std::ranges::sort(order, [&](std::size_t lhs, std::size_t rhs) {
        const float lhs_min = proxies[lhs].center[axis_index] - proxies[lhs].radius;
        const float rhs_min = proxies[rhs].center[axis_index] - proxies[rhs].radius;
        if (lhs_min != rhs_min) {
            return lhs_min < rhs_min;
        }
        return lhs < rhs;
    });

    std::vector<SapCandidatePair> result;
    std::vector<std::size_t> active;
    for (const auto current : order) {
        const float current_min = proxies[current].center[axis_index] - proxies[current].radius;
        std::erase_if(active, [&](std::size_t idx) {
            return (proxies[idx].center[axis_index] + proxies[idx].radius) < current_min;
        });
        for (const auto other : active) {
            if (sap_aabb_overlap(proxies[current], proxies[other])) {
                result.push_back(
                    SapCandidatePair{.first = std::min(current, other), .second = std::max(current, other)});
            }
        }
        active.push_back(current);
    }
    return result;
}

std::optional<std::size_t>& sap_small_domain_threshold_override() noexcept {
    static std::optional<std::size_t> override_value;
    return override_value;
}

template <std::size_t N>
void sap_sync(const std::vector<SapInternalProxy<N>>& proxies,
             std::size_t small_domain_threshold,
             int& primary_axis,
             std::vector<SapCandidatePair>& candidates) {
    const auto effective_threshold = sap_small_domain_threshold_override().value_or(small_domain_threshold);
    primary_axis = sap_select_primary_axis(proxies);
    candidates   = proxies.size() < effective_threshold ? sap_candidates_brute_force(proxies)
                                                         : sap_candidates_swept(proxies, primary_axis);
}

}  // namespace

std::optional<std::size_t> sap_small_domain_threshold_override_for_testing() noexcept {
    return sap_small_domain_threshold_override();
}

void set_sap_small_domain_threshold_override_for_testing(std::optional<std::size_t> threshold) noexcept {
    sap_small_domain_threshold_override() = threshold;
}

void SapBroadPhase2D::sync(std::span<const Proxy2D> proxies) {
    std::vector<SapInternalProxy<2>> internal;
    internal.reserve(proxies.size());
    for (const auto& proxy : proxies) {
        internal.push_back(SapInternalProxy<2>{.center = {proxy.center.x, proxy.center.y}, .radius = proxy.radius});
    }
    sap_sync(internal, small_domain_threshold_, primary_axis_, candidates_);
}

std::span<const SapCandidatePair> SapBroadPhase2D::candidate_pairs() const noexcept {
    return candidates_;
}

int SapBroadPhase2D::primary_axis_for_testing() const noexcept {
    return primary_axis_;
}

void SapBroadPhase2D::set_small_domain_threshold_for_testing(std::size_t threshold) noexcept {
    small_domain_threshold_ = threshold;
}

void SapBroadPhase3D::sync(std::span<const Proxy3D> proxies) {
    std::vector<SapInternalProxy<3>> internal;
    internal.reserve(proxies.size());
    for (const auto& proxy : proxies) {
        internal.push_back(SapInternalProxy<3>{.center = {proxy.center.x, proxy.center.y, proxy.center.z},
                                               .radius  = proxy.radius});
    }
    sap_sync(internal, small_domain_threshold_, primary_axis_, candidates_);
}

std::span<const SapCandidatePair> SapBroadPhase3D::candidate_pairs() const noexcept {
    return candidates_;
}

int SapBroadPhase3D::primary_axis_for_testing() const noexcept {
    return primary_axis_;
}

void SapBroadPhase3D::set_small_domain_threshold_for_testing(std::size_t threshold) noexcept {
    small_domain_threshold_ = threshold;
}

namespace {

struct SapDirectedTuple {
    std::uint64_t left_ordinal;
    std::uint64_t right_ordinal;
    std::size_t left_index;
    std::size_t right_index;
};

template <typename Proxy>
void sap_execute_pair_tuples_impl(std::span<const Proxy> proxies,
                                  std::span<const SapCandidatePair> candidates,
                                  const std::function<void(entt::entity, entt::entity)>& on_tuple) {
    std::vector<SapDirectedTuple> tuples;
    tuples.reserve((candidates.size() * 2) + proxies.size());
    for (const auto& candidate : candidates) {
        tuples.push_back(SapDirectedTuple{.left_ordinal  = proxies[candidate.first].ordinal,
                                          .right_ordinal = proxies[candidate.second].ordinal,
                                          .left_index    = candidate.first,
                                          .right_index   = candidate.second});
        tuples.push_back(SapDirectedTuple{.left_ordinal  = proxies[candidate.second].ordinal,
                                          .right_ordinal = proxies[candidate.first].ordinal,
                                          .left_index    = candidate.second,
                                          .right_index   = candidate.first});
    }
    // Self-tuples: SAP structurally cannot report a proxy paired with itself,
    // so every live proxy's self-tuple is synthesized unconditionally here,
    // exactly as the Cartesian pair-handler loop already includes (a, a)
    // among its N×N tuples (spatial-broadphase-runtime, dsl-pair-relations).
    for (std::size_t i = 0; i < proxies.size(); ++i) {
        tuples.push_back(SapDirectedTuple{
            .left_ordinal = proxies[i].ordinal, .right_ordinal = proxies[i].ordinal, .left_index = i, .right_index = i});
    }

    std::ranges::sort(tuples, [](const SapDirectedTuple& lhs, const SapDirectedTuple& rhs) {
        if (lhs.left_ordinal != rhs.left_ordinal) {
            return lhs.left_ordinal < rhs.left_ordinal;
        }
        return lhs.right_ordinal < rhs.right_ordinal;
    });

    for (const auto& tuple : tuples) {
        on_tuple(proxies[tuple.left_index].entity, proxies[tuple.right_index].entity);
    }
}

}  // namespace

void sap_execute_pair_tuples(std::span<const Proxy2D> proxies,
                             std::span<const SapCandidatePair> candidates,
                             const std::function<void(entt::entity, entt::entity)>& on_tuple) {
    sap_execute_pair_tuples_impl(proxies, candidates, on_tuple);
}

void sap_execute_pair_tuples(std::span<const Proxy3D> proxies,
                             std::span<const SapCandidatePair> candidates,
                             const std::function<void(entt::entity, entt::entity)>& on_tuple) {
    sap_execute_pair_tuples_impl(proxies, candidates, on_tuple);
}

// ── Frame-local consumed input (editor input override) ────────────────────────

namespace {

std::unordered_set<int>& consumed_key_codes() noexcept {
    static std::unordered_set<int> codes;
    return codes;
}

std::unordered_set<int>& consumed_mouse_codes() noexcept {
    static std::unordered_set<int> codes;
    return codes;
}

}  // namespace

void reset_consumed_input() noexcept {
    consumed_key_codes().clear();
    consumed_mouse_codes().clear();
}

void mark_input_key_consumed(const int key) noexcept {
    if (key != 0) {
        consumed_key_codes().insert(key);
    }
}

void mark_input_mouse_consumed(const int mouse_button) noexcept {
    if (mouse_button >= 0) {
        consumed_mouse_codes().insert(mouse_button);
    }
}

bool is_input_key_consumed(const int key) noexcept {
    return consumed_key_codes().contains(key);
}

bool is_input_mouse_consumed(const int mouse_button) noexcept {
    return consumed_mouse_codes().contains(mouse_button);
}

// ── Active camera state ───────────────────────────────────────────────────────

void set_active_camera_2d(Camera2D cam) noexcept {
    active_camera_2d_storage() = cam;
}

Camera2D get_active_camera_2d() noexcept {
    return active_camera_2d_storage();
}

void set_active_camera_3d(Camera3D cam) noexcept {
    active_camera_3d_storage() = cam;
}

Camera3D get_active_camera_3d() noexcept {
    return active_camera_3d_storage();
}

void draw_shape_rectangle(const Vector2 position,
                          const Vector2 size,
                          const Vector2 origin,
                          const float rotation_rad,
                          const Color color) noexcept {
    constexpr float kRadToDeg = 180.0F / std::numbers::pi_v<float>;
    cactus::runtime::raylib::DrawRectanglePro(
        Rectangle{.x = position.x, .y = position.y, .width = size.x, .height = size.y},
        origin,
        rotation_rad * kRadToDeg,
        color);
}

void draw_shape_circle(const Vector2 position,
                       const float diameter,
                       const Vector2 origin,
                       const Color color) noexcept {
    cactus::runtime::raylib::DrawCircleV(Vector2{.x = position.x - origin.x, .y = position.y - origin.y},
                                         diameter / 2.0F, color);
}

Shader* ensure_render_pass_shader(RenderPassShaderState& state, const char* vertex_src, const char* fragment_src) {
    if (state.loaded) {
        return &state.shader;
    }
    if (state.attempted) {
        // Already tried and failed (compile error or no GL context yet) —
        // don't retry every frame; mirrors the lighting shader's contract.
        return nullptr;
    }
    state.attempted = true;
#ifdef CACTUS_RAYLIB_FAKE
    // LoadShaderFromMemory needs a real GL context, which headless behavioral
    // tests never create — the render-pass phase's draw step simply never
    // runs under the fake, same disclosed limitation as the lighting shader.
    (void)vertex_src;
    (void)fragment_src;
    return nullptr;
#else
    if (!cactus::runtime::raylib::IsWindowReady()) {
        return nullptr;
    }
    state.shader = LoadShaderFromMemory(vertex_src, fragment_src);
    if (!IsShaderValid(state.shader)) {
        state.shader = {};
        return nullptr;
    }
    state.loaded = true;
    return &state.shader;
#endif
}

void draw_render_pass_quad_instance() noexcept {
    // Corner/uv per vertex, matching render_pass_emitter.cpp's Quads layout.
    // Fed through raylib's own vertexPosition/vertexTexCoord attributes
    // (rlVertex2f/rlTexCoord2f), not gl_VertexID: rlgl is a batching
    // renderer, so gl_VertexID reflects the shared batch's cumulative vertex
    // offset, not a small per-instance index.
    //
    // RL_QUADS, not RL_TRIANGLES: empirically, this GL driver silently drops
    // an RL_TRIANGLES batch whose vertices end up at a large on-screen
    // position after the vertex shader's transform (reproduced with a
    // screenshot-diffing probe harness — a bare RL_TRIANGLES draw through
    // raylib's own default shader also went missing, while the identical
    // geometry through RL_QUADS rendered correctly). RL_QUADS is also
    // raylib's own preferred mode for this kind of untextured quad
    // (DrawRectangleRec/DrawTriangle use it via SUPPORT_QUADS_DRAW_MODE).
    constexpr std::array<Vector2, 4> kCorners = {
        Vector2{.x = -1.0F, .y = -1.0F},
        Vector2{.x = -1.0F, .y = 1.0F},
        Vector2{.x = 1.0F, .y = 1.0F},
        Vector2{.x = 1.0F, .y = -1.0F},
    };
    constexpr std::array<Vector2, 4> kUvs = {
        Vector2{.x = 0.0F, .y = 0.0F},
        Vector2{.x = 0.0F, .y = 1.0F},
        Vector2{.x = 1.0F, .y = 1.0F},
        Vector2{.x = 1.0F, .y = 0.0F},
    };
    rlBegin(RL_QUADS);
    rlNormal3f(0.0F, 0.0F, 1.0F);
    for (int i = 0; i < 4; ++i) {
        rlTexCoord2f(kUvs.at(static_cast<std::size_t>(i)).x, kUvs.at(static_cast<std::size_t>(i)).y);
        rlVertex2f(kCorners.at(static_cast<std::size_t>(i)).x, kCorners.at(static_cast<std::size_t>(i)).y);
    }
    rlEnd();
    // Force this instance's quad to actually submit to the GPU now, before
    // the caller uploads the next entity's uniform values. Without this,
    // rlgl defers the draw call into a shared batch (same shader/draw mode
    // across the whole dispatch loop never trips its auto-flush), so every
    // queued instance ends up rendered with whichever uniform value was
    // current at the batch's eventual flush — the *last* entity's, not its
    // own.
    rlDrawRenderBatchActive();
}

// ── Editor extern func implementations (std.editor) ──────────────────────────

void register_editor_hit_test_impl(EditorHitTestImpl fn) noexcept {
    hit_test_impl_storage() = std::move(fn);
}

void register_editor_spawn_impl(EditorSpawnImpl fn) noexcept {
    spawn_impl_storage() = std::move(fn);
}

void register_editor_raycast_impl(EditorRaycastImpl fn) noexcept {
    raycast_impl_storage() = std::move(fn);
}

void register_editor_active_mode_impl(EditorActiveModeImpl fn) noexcept {
    active_mode_impl_storage() = std::move(fn);
}

void register_editor_is_active_impl(EditorIsActiveImpl fn) noexcept {
    is_active_impl_storage() = std::move(fn);
}

entt::entity editor_spawn_template(entt::registry& registry,
                                   const std::string& template_name,
                                   Vector2 position_2d,
                                   Vector3 position_3d) noexcept {
    if (spawn_impl_storage()) {
        return spawn_impl_storage()(registry, template_name, position_2d, position_3d);
    }
    return entt::entity{entt::null};
}

entt::entity editor_hit_test_2d(entt::registry& registry, Vector2 screen_pos, int mask) noexcept {
    const Vector2 world_pos = editor_screen_to_world_2d(screen_pos);
    if (hit_test_impl_storage()) {
        return hit_test_impl_storage()(registry, world_pos, mask);
    }
    return entt::entity{entt::null};
}

entt::entity editor_raycast_3d(entt::registry& registry, Vector2 screen_pos, int mask) noexcept {
    if (raycast_impl_storage()) {
        const Ray ray = GetScreenToWorldRay(screen_pos, get_active_camera_3d());
        return raycast_impl_storage()(registry, ray, mask);
    }
    return entt::entity{entt::null};
}

void register_pointer_window_candidates_impl(PointerCandidatesImpl fn) noexcept {
    pointer_window_candidates_impl_storage() = std::move(fn);
}

void register_pointer_world_candidates_impl(PointerCandidatesImpl fn) noexcept {
    pointer_world_candidates_impl_storage() = std::move(fn);
}

entt::entity pointer_top_target(entt::registry& registry) noexcept {
    const Vector2 pointer_position = cactus::runtime::raylib::GetMousePosition();
    std::vector<PointerCandidate> ordered;
    if (pointer_window_candidates_impl_storage()) {
        auto window = pointer_window_candidates_impl_storage()(registry, pointer_position);
        ordered.insert(ordered.end(), window.begin(), window.end());
    }
    if (pointer_world_candidates_impl_storage()) {
        auto world = pointer_world_candidates_impl_storage()(registry, pointer_position);
        ordered.insert(ordered.end(), world.begin(), world.end());
    }
    return resolve_pointer_target(ordered);
}

void reset_pointer_router_state() noexcept {
    pointer_hovered_entity_storage()  = entt::entity{entt::null};
    pointer_captured_entity_storage() = entt::entity{entt::null};
}

PointerFrameTransitions compute_pointer_frame_transitions(entt::registry& registry) noexcept {
    PointerFrameTransitions result;

    // Stale-safe: an entity destroyed since it was hovered/captured is
    // cleared silently before this frame's decisions — no event is ever
    // constructed for a dead entity (spec.md "routing safely clears its
    // stored hover handle"/"the capture is cleared safely").
    auto& hovered  = pointer_hovered_entity_storage();
    auto& captured = pointer_captured_entity_storage();
    if (hovered != entt::entity{entt::null} && !registry.valid(hovered)) {
        hovered = entt::entity{entt::null};
    }
    if (captured != entt::entity{entt::null} && !registry.valid(captured)) {
        captured = entt::entity{entt::null};
    }

    const entt::entity top = pointer_top_target(registry);
    result.top              = top;

    if (top != hovered) {
        result.hover_changed = true;
        result.leave_target  = hovered;
        result.enter_target  = top;
        hovered               = top;
    }

    const bool pressed_now  = cactus::runtime::raylib::IsMouseButtonPressed(kPointerPrimaryMouseButton);
    const bool released_now = cactus::runtime::raylib::IsMouseButtonReleased(kPointerPrimaryMouseButton);

    if (pressed_now && captured == entt::entity{entt::null}) {
        // A miss (top == null) leaves the logical action unconsumed
        // (spec.md "Miss leaves gameplay input available").
        if (top != entt::entity{entt::null}) {
            captured                       = top;
            result.press_occurred          = true;
            result.press_target            = top;
            result.should_consume_primary  = true;
        }
    } else if (captured != entt::entity{entt::null}) {
        // An active capture consumes every frame it's held, not only its
        // press/release edges (spec.md "owns an active primary capture").
        result.should_consume_primary = true;
    }

    if (released_now && captured != entt::entity{entt::null}) {
        result.release_occurred       = true;
        result.release_target         = captured;
        result.release_is_click       = (top == captured);
        result.should_consume_primary = true;
        captured                       = entt::entity{entt::null};
    }

    return result;
}

int editor_active_mode(entt::registry& registry) noexcept {
    if (active_mode_impl_storage()) {
        return active_mode_impl_storage()(registry);
    }
    return 0;
}

bool editor_is_active(entt::registry& registry) noexcept {
    if (is_active_impl_storage()) {
        return is_active_impl_storage()(registry);
    }
    return false;
}

Vector2 editor_screen_size() noexcept {
    return Vector2{.x = static_cast<float>(cactus::runtime::raylib::GetScreenWidth()),
                   .y = static_cast<float>(cactus::runtime::raylib::GetScreenHeight())};
}

// ── Standard UI metric fact bridges (std.ui window_size/text_size/texture_size) ─
// Design decision #10: the backend owns platform facts (window bounds, font,
// and texture metrics), not authored Cactus. Each returns a safe deterministic
// fallback instead of touching platform state that may not exist yet (no
// window, no GPU context) — see design.md's "safe fallback values" phrasing.

Vector2 ui_window_size() noexcept {
    if (!cactus::runtime::raylib::IsWindowReady()) {
        return Vector2{.x = 0.0F, .y = 0.0F};
    }
    return Vector2{.x = static_cast<float>(cactus::runtime::raylib::GetScreenWidth()),
                   .y = static_cast<float>(cactus::runtime::raylib::GetScreenHeight())};
}

Vector2 ui_text_size(const std::string& value, const int font_size) noexcept {
    const float safe_font_size = font_size > 0 ? static_cast<float>(font_size) : 1.0F;
    if (!cactus::runtime::raylib::IsWindowReady()) {
        // No default font exists yet (it loads during window init); fall back
        // to the same deterministic approximation the fake uses once ready
        // (see raylib_io.hpp) rather than measuring against an empty Font.
        return Vector2{.x = static_cast<float>(value.size()) * safe_font_size * 0.5F, .y = safe_font_size};
    }
    return cactus::runtime::raylib::MeasureTextEx(GetFontDefault(), value.c_str(), safe_font_size, 1.0F);
}

// Real per-asset pixel dimensions are not yet tracked anywhere in the asset
// pipeline (texture registration only stores a path; ensure_texture_resource
// above materializes every handle as the same GPU placeholder) — a resolved
// handle reports a documented placeholder icon size rather than probing GPU
// state, matching the rest of the placeholder asset pipeline's fidelity.
Vector2 ui_texture_size(const AssetHandle texture) noexcept {
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, texture);
    if (!resolved.ready()) {
        return Vector2{.x = 0.0F, .y = 0.0F};
    }
    return Vector2{.x = 32.0F, .y = 32.0F};
}

namespace {
constexpr int kImageFitContain = 1;
constexpr int kImageFitCover   = 2;
constexpr int kTextAlignCenter = 1;
constexpr int kTextAlignEnd    = 2;
}  // namespace

ImageDrawRects compute_image_draw_rects(const int fit,
                                        const Vector2 dest_position,
                                        const Vector2 dest_size,
                                        const Vector2 texture_size,
                                        const int frame_index,
                                        const int frame_count) noexcept {
    const int safe_frame_count = frame_count > 0 ? frame_count : 1;
    const int safe_frame       = ((frame_index % safe_frame_count) + safe_frame_count) % safe_frame_count;
    const float frame_width    = texture_size.x / static_cast<float>(safe_frame_count);
    const float frame_height   = texture_size.y;
    const Rectangle frame_source{
        .x      = frame_width * static_cast<float>(safe_frame),
        .y      = 0.0F,
        .width  = frame_width,
        .height = frame_height,
    };
    const Rectangle full_dest{.x = dest_position.x, .y = dest_position.y, .width = dest_size.x, .height = dest_size.y};

    const bool has_extent = frame_width > 0.0F && frame_height > 0.0F && dest_size.x > 0.0F && dest_size.y > 0.0F;

    if (fit == kImageFitContain && has_extent) {
        const float scale = std::min(dest_size.x / frame_width, dest_size.y / frame_height);
        const Vector2 drawn_size{.x = frame_width * scale, .y = frame_height * scale};
        const Rectangle centered_dest{
            .x      = dest_position.x + ((dest_size.x - drawn_size.x) / 2.0F),
            .y      = dest_position.y + ((dest_size.y - drawn_size.y) / 2.0F),
            .width  = drawn_size.x,
            .height = drawn_size.y,
        };
        return ImageDrawRects{.source = frame_source, .dest = centered_dest};
    }

    if (fit == kImageFitCover && has_extent) {
        const float scale = std::max(dest_size.x / frame_width, dest_size.y / frame_height);
        const Vector2 crop_size{.x = dest_size.x / scale, .y = dest_size.y / scale};
        const Rectangle cropped_source{
            .x      = frame_source.x + ((frame_width - crop_size.x) / 2.0F),
            .y      = frame_source.y + ((frame_height - crop_size.y) / 2.0F),
            .width  = crop_size.x,
            .height = crop_size.y,
        };
        return ImageDrawRects{.source = cropped_source, .dest = full_dest};
    }

    // Stretch, or a degenerate (zero-extent) texture/dest that fit math can't
    // meaningfully scale — draw the whole frame across the whole destination.
    return ImageDrawRects{.source = frame_source, .dest = full_dest};
}

namespace {

[[nodiscard]] Color tint_with_opacity(const Color color, const float opacity) noexcept {
    return color_from_components(static_cast<float>(color.r) / 255.0F,
                                 static_cast<float>(color.g) / 255.0F,
                                 static_cast<float>(color.b) / 255.0F,
                                 (static_cast<float>(color.a) / 255.0F) * opacity);
}

void draw_ui_label(const std::string& text,
                   const int font_size,
                   const Color color,
                   const Rectangle bounds,
                   const int align) noexcept {
    if (text.empty()) {
        return;
    }
    const float safe_font_size = font_size > 0 ? static_cast<float>(font_size) : 1.0F;
    const Vector2 measured     = ui_text_size(text, font_size);
    float x                    = bounds.x;
    if (align == kTextAlignCenter) {
        x = bounds.x + ((bounds.width - measured.x) / 2.0F);
    } else if (align == kTextAlignEnd) {
        x = bounds.x + bounds.width - measured.x;
    }
    const Vector2 position{.x = x, .y = bounds.y + ((bounds.height - measured.y) / 2.0F)};
    cactus::runtime::raylib::DrawTextEx(GetFontDefault(), text.c_str(), position, safe_font_size, 1.0F, color);
}

void draw_ui_image(const UiPrimitive& primitive, const Rectangle bounds, const float opacity) noexcept {
    const auto resolved = shared_asset_registry().resolve(AssetKind::Texture, primitive.image_texture);
    if (!resolved.ready()) {
        return;
    }
    Texture2D* texture = ensure_texture_resource(resolved.runtime_id);
    if (texture == nullptr) {
        return;
    }
    const int frame_count = primitive.has_frame_animation ? primitive.frame_count : 1;
    const int frame       = primitive.has_frame_animation ? primitive.frame : 0;
    const auto rects      = compute_image_draw_rects(
        primitive.image_fit,
        Vector2{.x = bounds.x, .y = bounds.y},
        Vector2{.x = bounds.width, .y = bounds.height},
        Vector2{.x = static_cast<float>(texture->width), .y = static_cast<float>(texture->height)},
        frame,
        frame_count);
    cactus::runtime::raylib::DrawTexturePro(*texture,
                                            rects.source,
                                            rects.dest,
                                            Vector2{.x = 0.0F, .y = 0.0F},
                                            0.0F,
                                            tint_with_opacity(primitive.image_tint, opacity));
}

// disabled, pressed, hovered, then normal — spec.md "Button presentation
// derives from generic pointer state": generic PointerState (not a
// UI-specific routing state) drives presentation once std.pointer's router
// exists (section 9); until then PointerState is simply absent/false.
[[nodiscard]] Color button_fill_color(const UiPrimitive& primitive) noexcept {
    if (!primitive.effective_enabled) {
        return primitive.button_disabled_color;
    }
    if (primitive.pointer_pressed) {
        return primitive.button_pressed_color;
    }
    if (primitive.pointer_hovered) {
        return primitive.button_hover_color;
    }
    return primitive.button_normal_color;
}

}  // namespace

void render_ui_primitive(const UiPrimitive& primitive) noexcept {
    if (!primitive.effective_visible) {
        return;
    }
    if (primitive.clip_max.x <= primitive.clip_min.x || primitive.clip_max.y <= primitive.clip_min.y) {
        return;
    }

    cactus::runtime::raylib::BeginScissorMode(static_cast<int>(primitive.clip_min.x),
                                              static_cast<int>(primitive.clip_min.y),
                                              static_cast<int>(primitive.clip_max.x - primitive.clip_min.x),
                                              static_cast<int>(primitive.clip_max.y - primitive.clip_min.y));

    const float opacity = std::clamp(primitive.effective_opacity, 0.0F, 1.0F);

    // Visual.scale presents around the entity's own center pivot without
    // altering ComputedLayout's logical/hit rectangle (design decision #6).
    const Vector2 center{.x = primitive.position.x + (primitive.size.x / 2.0F),
                         .y = primitive.position.y + (primitive.size.y / 2.0F)};
    const Vector2 scaled_size{.x = primitive.size.x * primitive.visual_scale.x,
                              .y = primitive.size.y * primitive.visual_scale.y};
    const Rectangle bounds{
        .x      = center.x - (scaled_size.x / 2.0F),
        .y      = center.y - (scaled_size.y / 2.0F),
        .width  = scaled_size.x,
        .height = scaled_size.y,
    };

    if (primitive.has_panel && primitive.panel_background.a > 0) {
        cactus::runtime::raylib::DrawRectangleRec(bounds, tint_with_opacity(primitive.panel_background, opacity));
    }

    if (primitive.has_image) {
        draw_ui_image(primitive, bounds, opacity);
    }

    if (primitive.has_button) {
        cactus::runtime::raylib::DrawRectangleRec(bounds, tint_with_opacity(button_fill_color(primitive), opacity));
    }

    if (primitive.has_text) {
        draw_ui_label(primitive.text_value,
                      primitive.text_font_size,
                      tint_with_opacity(primitive.text_color, opacity),
                      bounds,
                      primitive.text_align);
    } else if (primitive.has_button) {
        draw_ui_label(
            primitive.button_label, 16, tint_with_opacity(primitive.button_text_color, opacity), bounds, kTextAlignCenter);
    }

    if (primitive.has_panel && primitive.panel_border_width > 0.0F && primitive.panel_border_color.a > 0) {
        cactus::runtime::raylib::DrawRectangleLinesEx(
            bounds, primitive.panel_border_width, tint_with_opacity(primitive.panel_border_color, opacity));
    }

    cactus::runtime::raylib::EndScissorMode();
    ++render_debug_state_storage().submitted_ui_primitives;
}

void sort_window_pointer_candidates(std::vector<PointerCandidate>& candidates) noexcept {
    std::ranges::stable_sort(
        candidates, [](const PointerCandidate& left, const PointerCandidate& right) noexcept {
            return left.draw_order > right.draw_order;
        });
}

void sort_flat_world_pointer_candidates(std::vector<PointerCandidate>& candidates) noexcept {
    std::ranges::stable_sort(candidates, [](const PointerCandidate& left, const PointerCandidate& right) noexcept {
        if (left.priority != right.priority) {
            return left.priority > right.priority;
        }
        return left.creation_ordinal < right.creation_ordinal;
    });
}

void sort_volume_world_pointer_candidates(std::vector<PointerCandidate>& candidates) noexcept {
    std::ranges::stable_sort(candidates, [](const PointerCandidate& left, const PointerCandidate& right) noexcept {
        return left.distance < right.distance;
    });
}

entt::entity resolve_pointer_target(const std::vector<PointerCandidate>& ordered_candidates) noexcept {
    for (const auto& candidate : ordered_candidates) {
        if (candidate.enabled) {
            return candidate.entity;
        }
        if (candidate.blocks_lower) {
            return entt::null;
        }
    }
    return entt::null;
}

bool point_in_rect(const Vector2 point, const Vector2 rect_min, const Vector2 rect_max) noexcept {
    return point.x >= rect_min.x && point.x <= rect_max.x && point.y >= rect_min.y && point.y <= rect_max.y;
}

bool point_in_flat_box(const Vector2 point, const Vector2 center, const Vector2 size) noexcept {
    const float half_x = size.x * 0.5F;
    const float half_y = size.y * 0.5F;
    return std::abs(point.x - center.x) <= half_x && std::abs(point.y - center.y) <= half_y;
}

bool point_in_flat_circle(const Vector2 point, const Vector2 center, const float radius) noexcept {
    const float dx = point.x - center.x;
    const float dy = point.y - center.y;
    return ((dx * dx) + (dy * dy)) <= (radius * radius);
}

Color editor_palette_color(const int index) noexcept {
    static constexpr std::array<Color, 6> kPalette{{
        {.r = 230, .g = 41, .b = 55, .a = 255},
        {.r = 0, .g = 228, .b = 48, .a = 255},
        {.r = 0, .g = 121, .b = 241, .a = 255},
        {.r = 253, .g = 249, .b = 0, .a = 255},
        {.r = 200, .g = 122, .b = 255, .a = 255},
        {.r = 255, .g = 161, .b = 0, .a = 255},
    }};
    const int remainder = ((index % 6) + 6) % 6;
    return kPalette[static_cast<std::size_t>(remainder)];
}

std::string editor_mode_label(const int mode) noexcept {
    switch (mode) {
        case 1:
            return "TRANSLATE";
        case 2:
            return "ROTATE";
        case 3:
            return "SCALE";
        case 4:
            return "PLACE";
        default:
            return "SELECT";
    }
}

float editor_palette_button_y(const int index) noexcept {
    return 40.0F + (static_cast<float>(index) * 30.0F);
}

Vector2 editor_screen_to_world_2d(Vector2 screen) noexcept {
    const Camera2D cam = get_active_camera_2d();
    return Vector2{
        .x = ((screen.x - cam.offset.x) / cam.zoom) + cam.target.x,
        .y = ((screen.y - cam.offset.y) / cam.zoom) + cam.target.y,
    };
}

Vector2 editor_mouse_delta_2d() noexcept {
    const Camera2D cam  = get_active_camera_2d();
    const Vector2 delta = cactus::runtime::raylib::GetMouseDelta();
    return Vector2{.x = delta.x / cam.zoom, .y = delta.y / cam.zoom};
}

Vector2 screen_delta_to_world_2d(Vector2 delta) noexcept {
    return editor_screen_to_world_2d(delta) -
           editor_screen_to_world_2d(Vector2{.x = 0.0F, .y = 0.0F});
}

std::optional<Vector3> editor_ray_plane_intersect(const Ray ray,
                                                  const Vector3 plane_origin,
                                                  const Vector3 plane_normal) noexcept {
    constexpr float kParallelEpsilon = 1e-6F;
    const float denominator          = Vector3DotProduct(ray.direction, plane_normal);
    if (std::abs(denominator) < kParallelEpsilon) {
        return std::nullopt;
    }
    const float t =
        Vector3DotProduct(Vector3Subtract(plane_origin, ray.position), plane_normal) / denominator;
    if (t < 0.0F) {
        return std::nullopt;
    }
    return Vector3Add(ray.position, Vector3Scale(ray.direction, t));
}

Vector3 editor_plane_project_3d(Vector2 screen, Vector3 plane_origin, Vector3 plane_normal) noexcept {
    const Ray ray = GetScreenToWorldRay(screen, get_active_camera_3d());
    return editor_ray_plane_intersect(ray, plane_origin, plane_normal).value_or(plane_origin);
}

Vector3 editor_mouse_delta_3d() noexcept {
    constexpr Vector3 kGroundOrigin{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    constexpr Vector3 kGroundNormal{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    const Camera3D cam    = get_active_camera_3d();
    const Vector2 cursor  = cactus::runtime::raylib::GetMousePosition();
    const Vector2 delta   = cactus::runtime::raylib::GetMouseDelta();
    const Vector2 previous{.x = cursor.x - delta.x, .y = cursor.y - delta.y};
    const auto current_hit =
        editor_ray_plane_intersect(GetScreenToWorldRay(cursor, cam), kGroundOrigin, kGroundNormal);
    const auto previous_hit =
        editor_ray_plane_intersect(GetScreenToWorldRay(previous, cam), kGroundOrigin, kGroundNormal);
    if (!current_hit || !previous_hit) {
        return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    }
    return Vector3Subtract(*current_hit, *previous_hit);
}

Vector3 screen_delta_on_plane_3d(const Vector2 screen,
                                 const Vector2 delta,
                                 const Vector3 plane_origin,
                                 const Vector3 plane_normal) noexcept {
    const Camera3D cam = get_active_camera_3d();
    const Vector2 previous{.x = screen.x - delta.x, .y = screen.y - delta.y};
    const auto current_hit =
        editor_ray_plane_intersect(GetScreenToWorldRay(screen, cam), plane_origin, plane_normal);
    const auto previous_hit =
        editor_ray_plane_intersect(GetScreenToWorldRay(previous, cam), plane_origin, plane_normal);
    if (!current_hit || !previous_hit) {
        return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    }
    return Vector3Subtract(*current_hit, *previous_hit);
}

// ── Camera rig lifecycle ──────────────────────────────────────────────────────

namespace {

EditorCameraEnterImpl& camera_enter_impl_storage() noexcept {
    static EditorCameraEnterImpl impl;
    return impl;
}
EditorCameraExitImpl& camera_exit_impl_storage() noexcept {
    static EditorCameraExitImpl impl;
    return impl;
}
EditorApplyCamera2DImpl& apply_camera_2d_impl_storage() noexcept {
    static EditorApplyCamera2DImpl impl;
    return impl;
}
EditorApplyCamera3DImpl& apply_camera_3d_impl_storage() noexcept {
    static EditorApplyCamera3DImpl impl;
    return impl;
}
EditorEntityPosition2DImpl& entity_position_2d_impl_storage() noexcept {
    static EditorEntityPosition2DImpl impl;
    return impl;
}
EditorEntityPosition3DImpl& entity_position_3d_impl_storage() noexcept {
    static EditorEntityPosition3DImpl impl;
    return impl;
}
entt::entity& camera_rig_entity_storage() noexcept {
    static entt::entity ent{entt::null};
    return ent;
}
std::vector<entt::entity>& saved_viewports_storage() noexcept {
    static std::vector<entt::entity> viewports;
    return viewports;
}

}  // namespace

void register_editor_camera_enter_impl(EditorCameraEnterImpl fn) noexcept {
    camera_enter_impl_storage() = std::move(fn);
}
void register_editor_camera_exit_impl(EditorCameraExitImpl fn) noexcept {
    camera_exit_impl_storage() = std::move(fn);
}
void register_editor_apply_camera_2d_impl(EditorApplyCamera2DImpl fn) noexcept {
    apply_camera_2d_impl_storage() = std::move(fn);
}
void register_editor_apply_camera_3d_impl(EditorApplyCamera3DImpl fn) noexcept {
    apply_camera_3d_impl_storage() = std::move(fn);
}
void register_editor_entity_position_2d_impl(EditorEntityPosition2DImpl fn) noexcept {
    entity_position_2d_impl_storage() = std::move(fn);
}
void register_editor_entity_position_3d_impl(EditorEntityPosition3DImpl fn) noexcept {
    entity_position_3d_impl_storage() = std::move(fn);
}

void set_editor_saved_viewports(std::vector<entt::entity> viewports) noexcept {
    saved_viewports_storage() = std::move(viewports);
}
const std::vector<entt::entity>& editor_saved_viewports() noexcept {
    return saved_viewports_storage();
}
entt::entity editor_rig_entity() noexcept {
    return camera_rig_entity_storage();
}

entt::entity editor_camera_enter(entt::registry& registry, bool use_3d) noexcept {
    if (camera_enter_impl_storage()) {
        camera_rig_entity_storage() = camera_enter_impl_storage()(registry, use_3d);
    }
    return camera_rig_entity_storage();
}
void editor_camera_exit(entt::registry& registry) noexcept {
    if (camera_exit_impl_storage()) {
        camera_exit_impl_storage()(registry, camera_rig_entity_storage());
    }
    camera_rig_entity_storage() = entt::entity{entt::null};
}
void editor_apply_camera_2d(entt::registry& registry, Vector2 view_center, float zoom) noexcept {
    if (apply_camera_2d_impl_storage()) {
        apply_camera_2d_impl_storage()(registry, camera_rig_entity_storage(), view_center, zoom);
    }
}
void editor_apply_camera_3d(entt::registry& registry, Vector3 position, Quat rotation) noexcept {
    if (apply_camera_3d_impl_storage()) {
        apply_camera_3d_impl_storage()(registry, camera_rig_entity_storage(), position, rotation);
    }
}
float editor_wheel_delta() noexcept {
    return cactus::runtime::raylib::GetMouseWheelMove();
}
Vector2 editor_mouse_delta_screen() noexcept {
    return cactus::runtime::raylib::GetMouseDelta();
}
Vector2 editor_entity_position_2d(entt::registry& registry, entt::entity entity_id) noexcept {
    if (entity_position_2d_impl_storage()) {
        return entity_position_2d_impl_storage()(registry, entity_id);
    }
    return Vector2{.x = 0.0F, .y = 0.0F};
}
Vector3 editor_entity_position_3d(entt::registry& registry, entt::entity entity_id) noexcept {
    if (entity_position_3d_impl_storage()) {
        return entity_position_3d_impl_storage()(registry, entity_id);
    }
    return Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
}

}  // namespace cactus::runtime::entt_backend
