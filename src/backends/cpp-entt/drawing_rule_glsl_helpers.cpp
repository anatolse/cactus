#include "backends/cpp-entt/drawing_rule_glsl_helpers.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace cactus {

namespace {

struct GlslHelper {
    std::string_view name;
    std::array<std::string_view, 2> needs;
    std::string_view text;
};

// In dependency order: a helper only calls helpers listed above it.
constexpr std::array kHelpers{
    GlslHelper{.name  = "nan",
               .needs = {},
               .text  = "float cactus_nan()\n"
                        "{\n"
                        "    return uintBitsToFloat(0x7fc00000u);\n"
                        "}\n"},
    // Built-in sin and cos are approximate on some GPUs (errors near 4e-5): reduce by pi/4 in
    // three parts (Cody-Waite) and use the Cephes polynomials, which stay near the CPU's result.
    GlslHelper{.name  = "sin_cos",
               .needs = {"nan"},
               .text  = "float cactus_sin_cos(float x, bool cosine)\n"
                        "{\n"
                        "    if (isnan(x) || isinf(x)) {\n"
                        "        return cactus_nan();\n"
                        "    }\n"
                        "    float a = abs(x);\n"
                        "    if (a > 1000000.0) {\n"
                        "        return cosine ? cos(x) : sin(x);\n"
                        "    }\n"
                        "    float sign = (!cosine && x < 0.0) ? -1.0 : 1.0;\n"
                        "    int j = int(a * 1.27323954473516);\n"
                        "    if ((j & 1) != 0) {\n"
                        "        j += 1;\n"
                        "    }\n"
                        "    float y = float(j);\n"
                        "    float r = ((a - y * 0.78515625) - y * 2.4187564849853515625e-4) - y * 3.77489497744594108e-8;\n"
                        "    j = j & 7;\n"
                        "    if (j > 3) {\n"
                        "        j -= 4;\n"
                        "        sign = -sign;\n"
                        "    }\n"
                        "    if (cosine && j > 1) {\n"
                        "        sign = -sign;\n"
                        "    }\n"
                        "    float z = r * r;\n"
                        "    float s = ((-1.9515295891e-4 * z + 8.3321608736e-3) * z - 1.6666654611e-1) * z * r + r;\n"
                        "    float c = ((2.443315711809948e-5 * z - 1.388731625493765e-3) * z + 4.166664568298827e-2) * z * z -\n"
                        "              0.5 * z + 1.0;\n"
                        "    bool odd_octant = j == 1 || j == 2;\n"
                        "    return sign * ((cosine != odd_octant) ? c : s);\n"
                        "}\n"},
    GlslHelper{.name  = "sin",
               .needs = {"sin_cos"},
               .text  = "float cactus_sin(float x)\n"
                        "{\n"
                        "    return cactus_sin_cos(x, false);\n"
                        "}\n"},
    GlslHelper{.name  = "cos",
               .needs = {"sin_cos"},
               .text  = "float cactus_cos(float x)\n"
                        "{\n"
                        "    return cactus_sin_cos(x, true);\n"
                        "}\n"},
    GlslHelper{.name  = "to_int",
               .needs = {},
               .text  = "int cactus_to_int(float v)\n"
                        "{\n"
                        "    if (isnan(v)) {\n"
                        "        return 0;\n"
                        "    }\n"
                        "    if (v >= 2147483648.0) {\n"
                        "        return 2147483647;\n"
                        "    }\n"
                        "    if (v <= -2147483648.0) {\n"
                        "        return -2147483647 - 1;\n"
                        "    }\n"
                        "    return int(v);\n"
                        "}\n"},
    GlslHelper{.name  = "round",
               .needs = {"to_int"},
               .text  = "int cactus_round(float v)\n"
                        "{\n"
                        "    float whole = trunc(v);\n"
                        "    if (abs(v - whole) >= 0.5) {\n"
                        "        whole += sign(v);\n"
                        "    }\n"
                        "    return cactus_to_int(whole);\n"
                        "}\n"},
    GlslHelper{.name  = "cactus_div",
               .needs = {},
               .text  = "int cactus_div(int a, int b)\n"
                        "{\n"
                        "    if (b == 0) {\n"
                        "        return 0;\n"
                        "    }\n"
                        "    uint ua = a < 0 ? uint(-a) : uint(a);\n"
                        "    uint ub = b < 0 ? uint(-b) : uint(b);\n"
                        "    uint uq = ua / ub;\n"
                        "    if ((a < 0) == (b < 0)) {\n"
                        "        return int(uq);\n"
                        "    }\n"
                        "    int q = -int(uq);\n"
                        "    if (uq * ub != ua) {\n"
                        "        q -= 1;\n"
                        "    }\n"
                        "    return q;\n"
                        "}\n"},
    GlslHelper{.name  = "cactus_mod",
               .needs = {"cactus_div"},
               .text  = "int cactus_mod(int a, int b)\n"
                        "{\n"
                        "    return a - cactus_div(a, b) * b;\n"
                        "}\n"},
    GlslHelper{.name  = "lerp",
               .needs = {},
               .text  = "float cactus_lerp(float a, float b, float t)\n"
                        "{\n"
                        "    return a + ((b - a) * t);\n"
                        "}\n"},
    GlslHelper{.name  = "lerp2",
               .needs = {},
               .text  = "vec2 cactus_lerp(vec2 a, vec2 b, float t)\n"
                        "{\n"
                        "    return a + ((b - a) * t);\n"
                        "}\n"},
    GlslHelper{.name  = "lerp3",
               .needs = {},
               .text  = "vec3 cactus_lerp(vec3 a, vec3 b, float t)\n"
                        "{\n"
                        "    return a + ((b - a) * t);\n"
                        "}\n"},
    GlslHelper{.name  = "clamp",
               .needs = {},
               .text  = "float cactus_clamp(float x, float lo, float hi)\n"
                        "{\n"
                        "    if (x < lo) {\n"
                        "        return lo;\n"
                        "    }\n"
                        "    if (x > hi) {\n"
                        "        return hi;\n"
                        "    }\n"
                        "    return x;\n"
                        "}\n"},
    GlslHelper{.name  = "clamp2",
               .needs = {"clamp"},
               .text  = "vec2 cactus_clamp(vec2 v, vec2 lo, vec2 hi)\n"
                        "{\n"
                        "    return vec2(cactus_clamp(v.x, lo.x, hi.x), cactus_clamp(v.y, lo.y, hi.y));\n"
                        "}\n"},
    GlslHelper{.name  = "clamp3",
               .needs = {"clamp"},
               .text  = "vec3 cactus_clamp(vec3 v, vec3 lo, vec3 hi)\n"
                        "{\n"
                        "    return vec3(cactus_clamp(v.x, lo.x, hi.x), cactus_clamp(v.y, lo.y, hi.y),\n"
                        "                cactus_clamp(v.z, lo.z, hi.z));\n"
                        "}\n"},
    GlslHelper{.name  = "min",
               .needs = {"nan"},
               .text  = "float cactus_min(float a, float b)\n"
                        "{\n"
                        "    if (isnan(a) || isnan(b)) {\n"
                        "        return cactus_nan();\n"
                        "    }\n"
                        "    return min(a, b);\n"
                        "}\n"},
    GlslHelper{.name  = "max",
               .needs = {"nan"},
               .text  = "float cactus_max(float a, float b)\n"
                        "{\n"
                        "    if (isnan(a) || isnan(b)) {\n"
                        "        return cactus_nan();\n"
                        "    }\n"
                        "    return max(a, b);\n"
                        "}\n"},
    GlslHelper{.name  = "sqrt",
               .needs = {"nan"},
               .text  = "float cactus_sqrt(float v)\n"
                        "{\n"
                        "    return v < 0.0 ? cactus_nan() : sqrt(v);\n"
                        "}\n"},
    GlslHelper{.name  = "atan2",
               .needs = {},
               .text  = "float cactus_atan2(float y, float x)\n"
                        "{\n"
                        "    if (x == 0.0 && y == 0.0) {\n"
                        "        float angle = (floatBitsToUint(x) >> 31u) != 0u ? 3.14159265 : 0.0;\n"
                        "        return (floatBitsToUint(y) >> 31u) != 0u ? -angle : angle;\n"
                        "    }\n"
                        "    return atan(y, x);\n"
                        "}\n"},
    GlslHelper{.name  = "pow",
               .needs = {"nan"},
               .text  = "float cactus_pow(float b, float e)\n"
                        "{\n"
                        "    if (e == 0.0 || b == 1.0) {\n"
                        "        return 1.0;\n"
                        "    }\n"
                        "    if (isnan(b) || isnan(e)) {\n"
                        "        return cactus_nan();\n"
                        "    }\n"
                        "    float magnitude = b == 0.0 ? (e > 0.0 ? 0.0 : uintBitsToFloat(0x7f800000u)) : pow(abs(b), e);\n"
                        "    if (b >= 0.0) {\n"
                        "        return magnitude;\n"
                        "    }\n"
                        "    if (floor(e) != e) {\n"
                        "        return cactus_nan();\n"
                        "    }\n"
                        "    return mod(e, 2.0) == 1.0 ? -magnitude : magnitude;\n"
                        "}\n"},
    GlslHelper{.name  = "normalize2",
               .needs = {},
               .text  = "vec2 cactus_normalize2(vec2 v)\n"
                        "{\n"
                        "    float len_sq = (v.x * v.x) + (v.y * v.y);\n"
                        "    if (len_sq <= 0.0) {\n"
                        "        return vec2(0.0, 0.0);\n"
                        "    }\n"
                        "    return v * (1.0 / sqrt(len_sq));\n"
                        "}\n"},
    GlslHelper{.name  = "normalize3",
               .needs = {},
               .text  = "vec3 cactus_normalize3(vec3 v)\n"
                        "{\n"
                        "    float len_sq = (v.x * v.x) + (v.y * v.y) + (v.z * v.z);\n"
                        "    if (len_sq <= 0.0) {\n"
                        "        return vec3(0.0, 0.0, 0.0);\n"
                        "    }\n"
                        "    return v * (1.0 / sqrt(len_sq));\n"
                        "}\n"},
    GlslHelper{.name  = "rotate2",
               .needs = {"sin", "cos"},
               .text  = "vec2 cactus_rotate2(vec2 v, float angle)\n"
                        "{\n"
                        "    float c = cactus_cos(angle);\n"
                        "    float s = cactus_sin(angle);\n"
                        "    return vec2((v.x * c) - (v.y * s), (v.x * s) + (v.y * c));\n"
                        "}\n"},
    // The axis-angle quaternion and raymath's Vector3RotateByQuaternion, as the CPU computes them.
    GlslHelper{.name  = "rotate3",
               .needs = {"sin", "cos"},
               .text  = "vec3 cactus_rotate3(vec3 v, vec3 axis, float angle)\n"
                        "{\n"
                        "    float len = sqrt(dot(axis, axis));\n"
                        "    if (len == 0.0) {\n"
                        "        return v;\n"
                        "    }\n"
                        "    float inv_len = 1.0 / len;\n"
                        "    float half_angle = angle * 0.5;\n"
                        "    float sin_half = cactus_sin(half_angle);\n"
                        "    vec4 q = vec4(axis.x * inv_len * sin_half, axis.y * inv_len * sin_half,\n"
                        "                  axis.z * inv_len * sin_half, cactus_cos(half_angle));\n"
                        "    float len_sq = dot(q, q);\n"
                        "    q = len_sq <= 0.0 ? vec4(0.0, 0.0, 0.0, 1.0) : q * (1.0 / sqrt(len_sq));\n"
                        "    return vec3(\n"
                        "        v.x * (q.x * q.x + q.w * q.w - q.y * q.y - q.z * q.z) + v.y * (2.0 * q.x * q.y - 2.0 * q.w * q.z) +\n"
                        "            v.z * (2.0 * q.x * q.z + 2.0 * q.w * q.y),\n"
                        "        v.x * (2.0 * q.w * q.z + 2.0 * q.x * q.y) + v.y * (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) +\n"
                        "            v.z * (-2.0 * q.w * q.x + 2.0 * q.y * q.z),\n"
                        "        v.x * (-2.0 * q.w * q.y + 2.0 * q.x * q.z) + v.y * (2.0 * q.w * q.x + 2.0 * q.y * q.z) +\n"
                        "            v.z * (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z));\n"
                        "}\n"},
};

void mark_with_needs(std::string_view name, std::vector<bool>& marked) {
    const auto found = std::ranges::find(kHelpers, name, &GlslHelper::name);
    if (found == kHelpers.end()) {
        return;
    }
    const auto index = static_cast<std::size_t>(found - kHelpers.begin());
    if (marked[index]) {
        return;
    }
    marked[index] = true;
    for (const auto need : found->needs) {
        if (!need.empty()) {
            mark_with_needs(need, marked);
        }
    }
}

}  // namespace

std::string glsl_helper_definitions(const std::unordered_set<std::string>& used) {
    std::vector<bool> marked(kHelpers.size(), false);
    for (const auto& name : used) {
        mark_with_needs(name, marked);
    }
    std::string text;
    for (std::size_t index = 0; index < kHelpers.size(); ++index) {
        if (marked[index]) {
            text += std::string(kHelpers[index].text) + "\n";
        }
    }
    return text;
}

}  // namespace cactus
