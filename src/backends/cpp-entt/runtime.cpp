#include "backends/cpp-entt/runtime.hpp"

#include <raylib.h>

#include <algorithm>
#include <array>
#include <memory_resource>
#include <raymath.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace cactus::runtime::entt_backend {

namespace {
RenderDebugState& render_debug_state_storage() noexcept {
    static RenderDebugState state;
    return state;
}

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
};

struct PointLightSubmission {
    Vector3 position{};
    Color color{};
    float intensity{1.0F};
    float range{10.0F};
};

struct TextureResourceEntry {
    Texture2D texture{};
    bool loaded{false};
    bool owned{false};
};

struct MeshResourceEntry {
    Mesh mesh{};
    bool loaded{false};
    bool owned{false};
};

struct MaterialResourceEntry {
    Material material{};
    bool loaded{false};
    bool owned{false};
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

std::pmr::vector<PointLightSubmission>& point_light_queue() noexcept {
    static std::pmr::vector<PointLightSubmission> queue{&render_queue_resource()};
    return queue;
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

constexpr int kMaxMeshLights  = 4;
constexpr int kPointLightType = 1;

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

std::string light_uniform_name(const int index, const std::string_view field) {
    std::string result{"lights["};
    result += std::to_string(index);
    result += "].";
    result += field;
    return result;
}

void reset_lighting_shader_state() noexcept {
    auto& state = lighting_shader_state();
    state       = {};
    state.enabled_locs.fill(-1);
    state.type_locs.fill(-1);
    state.position_locs.fill(-1);
    state.target_locs.fill(-1);
    state.color_locs.fill(-1);
}

Shader* ensure_lighting_shader() {
    auto& state = lighting_shader_state();
    if (state.loaded) {
        return &state.shader;
    }
    if (!IsWindowReady()) {
        return nullptr;
    }

    reset_lighting_shader_state();
    state.shader = LoadShaderFromMemory(kLightingVertexShader, kLightingFragmentShader);
    if (!IsShaderValid(state.shader)) {
        reset_lighting_shader_state();
        return nullptr;
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

    return &state.shader;
}

void clear_lighting_shader() noexcept {
    auto& state = lighting_shader_state();
    if (state.loaded && IsWindowReady()) {
        UnloadShader(state.shader);
    }
    reset_lighting_shader_state();
}

void apply_point_lights(const Camera3D& camera) {
    Shader* shader = ensure_lighting_shader();
    if (shader == nullptr) {
        return;
    }

    auto& shader_state = lighting_shader_state();
    const std::array<float, 4> ambient{0.22F, 0.22F, 0.28F, 1.0F};
    const std::array<float, 3> view_pos{camera.position.x, camera.position.y, camera.position.z};
    SetShaderValue(*shader, shader_state.ambient_loc, ambient.data(), SHADER_UNIFORM_VEC4);
    SetShaderValue(*shader, shader_state.view_pos_loc, view_pos.data(), SHADER_UNIFORM_VEC3);

    const int active_lights = std::min(static_cast<int>(point_light_queue().size()), kMaxMeshLights);
    render_debug_state_storage().active_point_lights  = active_lights;
    render_debug_state_storage().used_lit_mesh_shader = active_lights > 0;

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
        SetShaderValue(*shader, shader_state.enabled_locs[index], &enabled, SHADER_UNIFORM_INT);
        SetShaderValue(*shader, shader_state.type_locs[index], &type, SHADER_UNIFORM_INT);
        SetShaderValue(*shader, shader_state.position_locs[index], position.data(), SHADER_UNIFORM_VEC3);
        SetShaderValue(*shader, shader_state.target_locs[index], target.data(), SHADER_UNIFORM_VEC3);
        SetShaderValue(*shader, shader_state.color_locs[index], color.data(), SHADER_UNIFORM_VEC4);
    }
}

void note_missing_asset() noexcept {
    ++render_debug_state_storage().missing_assets;
}

void clear_texture_store() noexcept {
    for (auto& [runtime_id, entry] : textures()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadTexture(entry.texture);
        }
    }
    textures().clear();
}

void clear_mesh_store() noexcept {
    for (auto& [runtime_id, entry] : meshes()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadMesh(entry.mesh);
        }
    }
    meshes().clear();
}

void clear_material_store() noexcept {
    for (auto& [runtime_id, entry] : materials()) {
        (void)runtime_id;
        if (entry.loaded && entry.owned && IsWindowReady()) {
            UnloadMaterial(entry.material);
        }
    }
    materials().clear();
}

Texture2D* ensure_texture_resource(const int runtime_id) {
    if (runtime_id < 0) {
        return nullptr;
    }
    auto& entry = textures()[runtime_id];
    if (!entry.loaded) {
        if (!IsWindowReady()) {
            return nullptr;
        }
        Image image   = GenImageColor(1, 1, WHITE);
        entry.texture = LoadTextureFromImage(image);
        UnloadImage(image);
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
        if (!IsWindowReady()) {
            return nullptr;
        }
        entry.mesh = GenMeshCube(1.0F, 1.0F, 1.0F);
        UploadMesh(&entry.mesh, false);
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
        if (!IsWindowReady()) {
            return nullptr;
        }
        entry.material = LoadMaterialDefault();
        entry.loaded   = true;
        entry.owned    = true;
    }
    if (Shader* shader = ensure_lighting_shader(); shader != nullptr) {
        entry.material.shader = *shader;
    }
    entry.material.maps[MATERIAL_MAP_DIFFUSE].color = BLUE;
    return &entry.material;
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
    if (!IsWindowReady()) {
        return;
    }

    Camera3D camera{};
    camera.position   = Vector3{.x = 6.0F, .y = 6.0F, .z = 6.0F};
    camera.target     = Vector3{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    camera.up         = Vector3{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    camera.fovy       = 45.0F;
    camera.projection = CAMERA_PERSPECTIVE;

    apply_point_lights(camera);

    BeginMode3D(camera);
    for (const auto& submission : mesh_queue()) {
        Mesh* mesh         = ensure_mesh_resource(submission.mesh_runtime_id);
        Material* material = ensure_material_resource(submission.material_runtime_id);
        if (mesh == nullptr || material == nullptr) {
            continue;
        }
        DrawMesh(*mesh, *material, mesh_transform_matrix(submission));
    }
    EndMode3D();
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

    if (!IsWindowReady()) {
        return;
    }

    Camera2D camera{};
    camera.offset   = Vector2{.x = 0.0F, .y = 0.0F};
    camera.target   = Vector2{.x = 0.0F, .y = 0.0F};
    camera.rotation = 0.0F;
    camera.zoom     = 1.0F;

    BeginMode2D(camera);
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
        DrawTexturePro(*texture, src, dst, Vector2{.x = 0.0F, .y = 0.0F}, 0.0F, submission.color);
    }
    EndMode2D();
}
}  // namespace

RuntimeBinding bind_runtime(GeneratedProjectInfo project) noexcept {
    return RuntimeBinding{project};
}

void reset_render_debug_state() noexcept {
    render_debug_state_storage() = {};
    sprite_queue().clear();
    mesh_queue().clear();
    point_light_queue().clear();
    clear_texture_store();
    clear_mesh_store();
    clear_material_store();
    clear_lighting_shader();
    shared_asset_registry().clear_diagnostics();
}

const RenderDebugState& render_debug_state() noexcept {
    return render_debug_state_storage();
}

void begin_render_frame() noexcept {
    sprite_queue().clear();
    mesh_queue().clear();
    point_light_queue().clear();
    render_debug_state_storage().used_default_2d_camera = false;
    render_debug_state_storage().used_default_3d_camera = false;
    render_debug_state_storage().active_point_lights    = 0;
    render_debug_state_storage().used_lit_mesh_shader   = false;
    render_debug_state_storage().drawn_sprite_layers.clear();
}

void end_render_frame() noexcept {
    flush_mesh_queue();
    flush_sprite_queue();
    mesh_queue().clear();
    sprite_queue().clear();
    point_light_queue().clear();
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
                 const bool /*cast_shadow*/) noexcept {
    if (!visible) {
        return;
    }
    const auto mesh_resolved     = shared_asset_registry().resolve(AssetKind::Mesh, mesh);
    const auto material_resolved = shared_asset_registry().resolve(AssetKind::Material, material);
    if (!mesh_resolved.ready() || !material_resolved.ready()) {
        note_missing_asset();
        return;
    }
    mesh_queue().push_back(MeshSubmission{
        .position            = position,
        .rotation            = rotation,
        .scale               = scale,
        .mesh_runtime_id     = mesh_resolved.runtime_id,
        .material_runtime_id = material_resolved.runtime_id,
    });
    ++render_debug_state_storage().submitted_meshes;
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

void propagate_hierarchy(entt::registry& registry,
                         const std::function<bool(entt::entity)>& has_local_world,
                         const std::function<entt::entity(entt::entity)>& get_parent,
                         const std::function<void(entt::entity)>& copy_local,
                         const std::function<void(entt::entity, entt::entity)>& accumulate_from_parent) {
    std::pmr::monotonic_buffer_resource scratch_resource;
    std::pmr::vector<entt::entity> active_entities{&scratch_resource};

    std::function<void(entt::entity)> resolve;
    resolve = [&](entt::entity entity) -> void {
        const bool already_active = std::ranges::find(active_entities, entity) != active_entities.end();
        if (!registry.valid(entity) || already_active || !has_local_world(entity)) {
            return;
        }

        active_entities.push_back(entity);
        bool copied_local         = false;
        const entt::entity parent = get_parent(entity);
        if (parent != entt::null && registry.valid(parent) && has_local_world(parent)) {
            resolve(parent);
            accumulate_from_parent(parent, entity);
            copied_local = true;
        } else {
            copy_local(entity);
        }
        if (!copied_local) {
            copy_local(entity);
        }
        if (!active_entities.empty()) {
            active_entities.pop_back();
        }
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
    static std::pmr::vector<entt::entity> destroying_entities{&destroying_resource};
    const bool already_destroying = std::ranges::find(destroying_entities, entity) != destroying_entities.end();
    if (!registry.valid(entity) || already_destroying) {
        return;
    }

    destroying_entities.push_back(entity);
    std::pmr::monotonic_buffer_resource child_resource;
    std::pmr::vector<entt::entity> child_entities{&child_resource};
    visit_children(entity, [&](entt::entity child) { child_entities.push_back(child); });
    for (const auto child : child_entities) {
        destroy_entity_recursive(registry, child, visit_children);
    }
    if (registry.valid(entity)) {
        registry.destroy(entity);
    }
    if (!destroying_entities.empty()) {
        destroying_entities.pop_back();
    }
}

}  // namespace cactus::runtime::entt_backend