/*
 * math_util.hpp - Math Utilities
 *
 * Provides 2D geometry operations (Boost.Geometry wrapper) and
 * 3D math types (Vec3, Vec4, Quaternion, Mat3, Mat4, AABB, OBB, Sphere).
 */

#ifndef WINDOW_MATH_UTIL_HPP
#define WINDOW_MATH_UTIL_HPP

#include <cmath>
#include <algorithm>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/geometry/geometries/segment.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/linestring.hpp>
#include <boost/geometry/geometries/multi_point.hpp>
#include <boost/geometry/geometries/multi_polygon.hpp>
#include <boost/geometry/algorithms/distance.hpp>
#include <boost/geometry/algorithms/within.hpp>
#include <boost/geometry/algorithms/intersects.hpp>
#include <boost/geometry/algorithms/intersection.hpp>
#include <boost/geometry/algorithms/area.hpp>
#include <boost/geometry/algorithms/centroid.hpp>
#include <boost/geometry/algorithms/transform.hpp>
#include <boost/geometry/algorithms/convex_hull.hpp>
#include <boost/geometry/algorithms/envelope.hpp>
#include <boost/geometry/algorithms/simplify.hpp>
#include <boost/geometry/algorithms/correct.hpp>
#include <boost/geometry/algorithms/length.hpp>
#include <boost/geometry/algorithms/perimeter.hpp>
#include <boost/geometry/algorithms/union.hpp>
#include <boost/geometry/algorithms/difference.hpp>
#include <boost/geometry/algorithms/sym_difference.hpp>
#include <boost/geometry/algorithms/disjoint.hpp>
#include <boost/geometry/algorithms/touches.hpp>
#include <boost/geometry/algorithms/covered_by.hpp>
#include <boost/geometry/algorithms/equals.hpp>
#include <boost/geometry/algorithms/is_valid.hpp>
#include <boost/geometry/algorithms/num_points.hpp>
#include <boost/geometry/algorithms/reverse.hpp>

#include <vector>
#include <initializer_list>

// ============================================================================
// SIMD Platform Detection
// ============================================================================

#if defined(WINDOW_MATH_SSE)
    #include <xmmintrin.h>  // SSE
    #include <emmintrin.h>  // SSE2
    #include <smmintrin.h>  // SSE4.1
    #ifdef __FMA__
        #include <immintrin.h>
    #endif
    #define WINDOW_MATH_SIMD 1
#elif defined(WINDOW_MATH_NEON)
    #include <arm_neon.h>
    #define WINDOW_MATH_SIMD 1
#endif

#if defined(WINDOW_MATH_SVE)
    #include <arm_sve.h>
#endif

namespace window {
namespace math {

// ============================================================================
// SIMD Abstraction Layer (internal)
// ============================================================================

namespace simd {

#if defined(WINDOW_MATH_SSE)

using f128 = __m128;

inline f128 load4(const float* p)           { return _mm_loadu_ps(p); }
inline f128 load4a(const float* p)          { return _mm_load_ps(p); }
inline f128 load3(const float* p)           { return _mm_set_ps(0, p[2], p[1], p[0]); }
inline void store4(float* p, f128 v)        { _mm_storeu_ps(p, v); }
inline void store4a(float* p, f128 v)       { _mm_store_ps(p, v); }
inline f128 set(float x, float y, float z, float w) { return _mm_set_ps(w, z, y, x); }
inline f128 splat(float v)                  { return _mm_set1_ps(v); }
inline f128 zero()                          { return _mm_setzero_ps(); }

inline f128 add(f128 a, f128 b)             { return _mm_add_ps(a, b); }
inline f128 sub(f128 a, f128 b)             { return _mm_sub_ps(a, b); }
inline f128 mul(f128 a, f128 b)             { return _mm_mul_ps(a, b); }
inline f128 div(f128 a, f128 b)             { return _mm_div_ps(a, b); }
inline f128 neg(f128 a)                     { return _mm_sub_ps(_mm_setzero_ps(), a); }
inline f128 vmin(f128 a, f128 b)            { return _mm_min_ps(a, b); }
inline f128 vmax(f128 a, f128 b)            { return _mm_max_ps(a, b); }
inline f128 vabs(f128 a)                    { return _mm_andnot_ps(_mm_set1_ps(-0.0f), a); }
inline f128 vsqrt(f128 a)                   { return _mm_sqrt_ps(a); }

inline f128 madd(f128 a, f128 b, f128 c) {
#ifdef __FMA__
    return _mm_fmadd_ps(a, b, c);
#else
    return _mm_add_ps(_mm_mul_ps(a, b), c);
#endif
}

inline float dot4(f128 a, f128 b) {
    return _mm_cvtss_f32(_mm_dp_ps(a, b, 0xFF));
}

inline float dot3(f128 a, f128 b) {
    return _mm_cvtss_f32(_mm_dp_ps(a, b, 0x7F));
}

inline f128 cross3(f128 a, f128 b) {
    f128 a_yzx = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));
    f128 b_yzx = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 0, 2, 1));
    f128 c = _mm_sub_ps(_mm_mul_ps(a, b_yzx), _mm_mul_ps(a_yzx, b));
    return _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 0, 2, 1));
}

// Broadcast lane i across all lanes
template<int i>
inline f128 broadcast(f128 v) { return _mm_shuffle_ps(v, v, _MM_SHUFFLE(i, i, i, i)); }

// Mat4 column-major multiply: result = a * b
inline void mat4_mul(const float* a, const float* b, float* out) {
    for (int c = 0; c < 4; ++c) {
        f128 col = _mm_load_ps(&b[c * 4]);
        f128 x = broadcast<0>(col);
        f128 y = broadcast<1>(col);
        f128 z = broadcast<2>(col);
        f128 w = broadcast<3>(col);
        f128 result = _mm_mul_ps(_mm_load_ps(&a[0]), x);
        result = madd(_mm_load_ps(&a[4]), y, result);
        result = madd(_mm_load_ps(&a[8]), z, result);
        result = madd(_mm_load_ps(&a[12]), w, result);
        _mm_store_ps(&out[c * 4], result);
    }
}

// Mat4 * Vec4
inline f128 mat4_mul_vec(const float* m, f128 v) {
    f128 x = broadcast<0>(v);
    f128 y = broadcast<1>(v);
    f128 z = broadcast<2>(v);
    f128 w = broadcast<3>(v);
    f128 result = _mm_mul_ps(_mm_load_ps(&m[0]), x);
    result = madd(_mm_load_ps(&m[4]), y, result);
    result = madd(_mm_load_ps(&m[8]), z, result);
    result = madd(_mm_load_ps(&m[12]), w, result);
    return result;
}

#elif defined(WINDOW_MATH_NEON)

using f128 = float32x4_t;

inline f128 load4(const float* p)           { return vld1q_f32(p); }
inline f128 load4a(const float* p)          { return vld1q_f32(p); }
inline f128 load3(const float* p) {
    float32x2_t lo = vld1_f32(p);
    float32x2_t hi = vld1_lane_f32(p + 2, vdup_n_f32(0), 0);
    return vcombine_f32(lo, hi);
}
inline void store4(float* p, f128 v)        { vst1q_f32(p, v); }
inline void store4a(float* p, f128 v)       { vst1q_f32(p, v); }
inline f128 set(float x, float y, float z, float w) {
    float d[4] = {x, y, z, w};
    return vld1q_f32(d);
}
inline f128 splat(float v)                  { return vdupq_n_f32(v); }
inline f128 zero()                          { return vdupq_n_f32(0); }

inline f128 add(f128 a, f128 b)             { return vaddq_f32(a, b); }
inline f128 sub(f128 a, f128 b)             { return vsubq_f32(a, b); }
inline f128 mul(f128 a, f128 b)             { return vmulq_f32(a, b); }
inline f128 neg(f128 a)                     { return vnegq_f32(a); }
inline f128 vmin(f128 a, f128 b)            { return vminq_f32(a, b); }
inline f128 vmax(f128 a, f128 b)            { return vmaxq_f32(a, b); }
inline f128 vabs(f128 a)                    { return vabsq_f32(a); }

inline f128 div(f128 a, f128 b) {
#if defined(__aarch64__)
    return vdivq_f32(a, b);
#else
    f128 recip = vrecpeq_f32(b);
    recip = vmulq_f32(vrecpsq_f32(b, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(b, recip), recip);
    return vmulq_f32(a, recip);
#endif
}

inline f128 vsqrt(f128 a) {
#if defined(__aarch64__)
    return vsqrtq_f32(a);
#else
    f128 est = vrsqrteq_f32(a);
    est = vmulq_f32(vrsqrtsq_f32(vmulq_f32(a, est), est), est);
    return vmulq_f32(a, est);
#endif
}

inline f128 madd(f128 a, f128 b, f128 c) {
    return vmlaq_f32(c, a, b); // c + a*b
}

inline float hsum(f128 v) {
#if defined(__aarch64__)
    return vaddvq_f32(v);
#else
    float32x2_t s = vadd_f32(vget_low_f32(v), vget_high_f32(v));
    s = vpadd_f32(s, s);
    return vget_lane_f32(s, 0);
#endif
}

inline float dot4(f128 a, f128 b) { return hsum(vmulq_f32(a, b)); }

inline float dot3(f128 a, f128 b) {
    f128 m = vmulq_f32(a, b);
    m = vsetq_lane_f32(0, m, 3);
    return hsum(m);
}

inline f128 cross3(f128 a, f128 b) {
    // NEON lacks general shuffle; use scalar extraction
    float ax = vgetq_lane_f32(a, 0), ay = vgetq_lane_f32(a, 1), az = vgetq_lane_f32(a, 2);
    float bx = vgetq_lane_f32(b, 0), by = vgetq_lane_f32(b, 1), bz = vgetq_lane_f32(b, 2);
    float d[4] = {ay*bz - az*by, az*bx - ax*bz, ax*by - ay*bx, 0};
    return vld1q_f32(d);
}

// Broadcast lane i
template<int i>
inline f128 broadcast(f128 v) { return vdupq_laneq_f32(v, i); }

inline void mat4_mul(const float* a, const float* b, float* out) {
    for (int c = 0; c < 4; ++c) {
        f128 col = vld1q_f32(&b[c * 4]);
        f128 result = vmulq_laneq_f32(vld1q_f32(&a[0]),  col, 0);
        result = vmlaq_laneq_f32(result, vld1q_f32(&a[4]),  col, 1);
        result = vmlaq_laneq_f32(result, vld1q_f32(&a[8]),  col, 2);
        result = vmlaq_laneq_f32(result, vld1q_f32(&a[12]), col, 3);
        vst1q_f32(&out[c * 4], result);
    }
}

inline f128 mat4_mul_vec(const float* m, f128 v) {
    f128 result = vmulq_laneq_f32(vld1q_f32(&m[0]),  v, 0);
    result = vmlaq_laneq_f32(result, vld1q_f32(&m[4]),  v, 1);
    result = vmlaq_laneq_f32(result, vld1q_f32(&m[8]),  v, 2);
    result = vmlaq_laneq_f32(result, vld1q_f32(&m[12]), v, 3);
    return result;
}

#endif // WINDOW_MATH_NEON

} // namespace simd

namespace bg = boost::geometry;

// ============================================================================
// Core Types
// ============================================================================

// 2D vector/point type
using Vec2 = bg::model::d2::point_xy<float>;

// Box (axis-aligned bounding box)
using Box = bg::model::box<Vec2>;

// Line segment
using Segment = bg::model::segment<Vec2>;

// Polygon
using Polygon = bg::model::polygon<Vec2>;

// Ring (closed polygon boundary)
using Ring = bg::model::ring<Vec2>;

// Line string (polyline)
using LineString = bg::model::linestring<Vec2>;

// Multi-point
using MultiPoint = bg::model::multi_point<Vec2>;

// Multi-polygon
using MultiPolygon = bg::model::multi_polygon<Polygon>;

// ============================================================================
// Constants
// ============================================================================

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float HALF_PI = 1.57079632679489661923f;
constexpr float EPSILON = 1e-6f;

// ============================================================================
// Vec2 Component Access
// ============================================================================

inline float x(const Vec2& v) { return bg::get<0>(v); }
inline float y(const Vec2& v) { return bg::get<1>(v); }

inline void set_x(Vec2& v, float val) { bg::set<0>(v, val); }
inline void set_y(Vec2& v, float val) { bg::set<1>(v, val); }

// Size-style aliases
inline float width(const Vec2& v) { return bg::get<0>(v); }
inline float height(const Vec2& v) { return bg::get<1>(v); }

// ============================================================================
// Vec2 Construction
// ============================================================================

inline Vec2 vec2(float x, float y) { return Vec2(x, y); }
inline Vec2 vec2(float v) { return Vec2(v, v); }
inline Vec2 vec2_zero() { return Vec2(0.0f, 0.0f); }
inline Vec2 vec2_one() { return Vec2(1.0f, 1.0f); }
inline Vec2 vec2_up() { return Vec2(0.0f, -1.0f); }
inline Vec2 vec2_down() { return Vec2(0.0f, 1.0f); }
inline Vec2 vec2_left() { return Vec2(-1.0f, 0.0f); }
inline Vec2 vec2_right() { return Vec2(1.0f, 0.0f); }

// ============================================================================
// Vec2 Arithmetic Operations
// ============================================================================

inline Vec2 add(const Vec2& a, const Vec2& b) {
    return Vec2(x(a) + x(b), y(a) + y(b));
}

inline Vec2 sub(const Vec2& a, const Vec2& b) {
    return Vec2(x(a) - x(b), y(a) - y(b));
}

inline Vec2 mul(const Vec2& v, float s) {
    return Vec2(x(v) * s, y(v) * s);
}

inline Vec2 mul(const Vec2& a, const Vec2& b) {
    return Vec2(x(a) * x(b), y(a) * y(b));
}

inline Vec2 div(const Vec2& v, float s) {
    return Vec2(x(v) / s, y(v) / s);
}

inline Vec2 div(const Vec2& a, const Vec2& b) {
    return Vec2(x(a) / x(b), y(a) / y(b));
}

inline Vec2 neg(const Vec2& v) {
    return Vec2(-x(v), -y(v));
}

// ============================================================================
// Vec2 Operators (for convenience)
// ============================================================================

inline Vec2 operator+(const Vec2& a, const Vec2& b) { return add(a, b); }
inline Vec2 operator-(const Vec2& a, const Vec2& b) { return sub(a, b); }
inline Vec2 operator*(const Vec2& v, float s) { return mul(v, s); }
inline Vec2 operator*(float s, const Vec2& v) { return mul(v, s); }
inline Vec2 operator*(const Vec2& a, const Vec2& b) { return mul(a, b); }
inline Vec2 operator/(const Vec2& v, float s) { return div(v, s); }
inline Vec2 operator/(const Vec2& a, const Vec2& b) { return div(a, b); }
inline Vec2 operator-(const Vec2& v) { return neg(v); }

inline Vec2& operator+=(Vec2& a, const Vec2& b) { a = add(a, b); return a; }
inline Vec2& operator-=(Vec2& a, const Vec2& b) { a = sub(a, b); return a; }
inline Vec2& operator*=(Vec2& v, float s) { v = mul(v, s); return v; }
inline Vec2& operator/=(Vec2& v, float s) { v = div(v, s); return v; }

inline bool operator==(const Vec2& a, const Vec2& b) {
    return x(a) == x(b) && y(a) == y(b);
}

inline bool operator!=(const Vec2& a, const Vec2& b) {
    return !(a == b);
}

// ============================================================================
// Vec2 Vector Math
// ============================================================================

// Dot product
inline float dot(const Vec2& a, const Vec2& b) {
    return x(a) * x(b) + y(a) * y(b);
}

// Cross product (returns scalar for 2D - z component of 3D cross)
inline float cross(const Vec2& a, const Vec2& b) {
    return x(a) * y(b) - y(a) * x(b);
}

// Squared length (avoids sqrt)
inline float length_squared(const Vec2& v) {
    return dot(v, v);
}

// Length (magnitude)
inline float length(const Vec2& v) {
    return std::sqrt(length_squared(v));
}

// Distance between two points
inline float distance(const Vec2& a, const Vec2& b) {
    return length(sub(b, a));
}

// Squared distance (avoids sqrt)
inline float distance_squared(const Vec2& a, const Vec2& b) {
    return length_squared(sub(b, a));
}

// Normalize (returns zero vector if length is zero)
inline Vec2 normalize(const Vec2& v) {
    float len = length(v);
    if (len < EPSILON) return vec2_zero();
    return div(v, len);
}

// Safe normalize with fallback
inline Vec2 normalize_or(const Vec2& v, const Vec2& fallback) {
    float len = length(v);
    if (len < EPSILON) return fallback;
    return div(v, len);
}

// Perpendicular vector (rotated 90 degrees counter-clockwise)
inline Vec2 perpendicular(const Vec2& v) {
    return Vec2(-y(v), x(v));
}

// Perpendicular vector (rotated 90 degrees clockwise)
inline Vec2 perpendicular_cw(const Vec2& v) {
    return Vec2(y(v), -x(v));
}

// Reflect vector around normal
inline Vec2 reflect(const Vec2& v, const Vec2& normal) {
    return sub(v, mul(normal, 2.0f * dot(v, normal)));
}

// Project vector a onto vector b
inline Vec2 project(const Vec2& a, const Vec2& b) {
    float len_sq = length_squared(b);
    if (len_sq < EPSILON) return vec2_zero();
    return mul(b, dot(a, b) / len_sq);
}

// Reject vector a from vector b (component of a perpendicular to b)
inline Vec2 reject(const Vec2& a, const Vec2& b) {
    return sub(a, project(a, b));
}

// ============================================================================
// Vec2 Interpolation
// ============================================================================

// Linear interpolation
inline Vec2 lerp(const Vec2& a, const Vec2& b, float t) {
    return add(a, mul(sub(b, a), t));
}

// Smooth step interpolation
inline float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline Vec2 smoothstep(const Vec2& a, const Vec2& b, float t) {
    float s = smoothstep(0.0f, 1.0f, t);
    return lerp(a, b, s);
}

// Bezier interpolation (quadratic)
inline Vec2 bezier_quadratic(const Vec2& p0, const Vec2& p1, const Vec2& p2, float t) {
    float u = 1.0f - t;
    return add(add(mul(p0, u * u), mul(p1, 2.0f * u * t)), mul(p2, t * t));
}

// Bezier interpolation (cubic)
inline Vec2 bezier_cubic(const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, float t) {
    float u = 1.0f - t;
    float uu = u * u;
    float tt = t * t;
    return add(add(add(
        mul(p0, uu * u),
        mul(p1, 3.0f * uu * t)),
        mul(p2, 3.0f * u * tt)),
        mul(p3, tt * t));
}

// ============================================================================
// Vec2 Min/Max/Clamp
// ============================================================================

inline Vec2 min(const Vec2& a, const Vec2& b) {
    return Vec2(std::min(x(a), x(b)), std::min(y(a), y(b)));
}

inline Vec2 max(const Vec2& a, const Vec2& b) {
    return Vec2(std::max(x(a), x(b)), std::max(y(a), y(b)));
}

inline Vec2 clamp(const Vec2& v, const Vec2& min_val, const Vec2& max_val) {
    return max(min_val, min(v, max_val));
}

inline Vec2 clamp_length(const Vec2& v, float max_length) {
    float len = length(v);
    if (len > max_length && len > EPSILON) {
        return mul(v, max_length / len);
    }
    return v;
}

inline Vec2 abs(const Vec2& v) {
    return Vec2(std::abs(x(v)), std::abs(y(v)));
}

inline Vec2 floor(const Vec2& v) {
    return Vec2(std::floor(x(v)), std::floor(y(v)));
}

inline Vec2 ceil(const Vec2& v) {
    return Vec2(std::ceil(x(v)), std::ceil(y(v)));
}

inline Vec2 round(const Vec2& v) {
    return Vec2(std::round(x(v)), std::round(y(v)));
}

// ============================================================================
// Vec2 Angle Operations
// ============================================================================

// Angle of vector (radians, from positive x-axis)
inline float angle(const Vec2& v) {
    return std::atan2(y(v), x(v));
}

// Angle between two vectors (radians)
inline float angle_between(const Vec2& a, const Vec2& b) {
    float d = dot(normalize(a), normalize(b));
    return std::acos(std::clamp(d, -1.0f, 1.0f));
}

// Signed angle from a to b (radians, positive = counter-clockwise)
inline float signed_angle(const Vec2& a, const Vec2& b) {
    return std::atan2(cross(a, b), dot(a, b));
}

// Create unit vector from angle (radians)
inline Vec2 from_angle(float radians) {
    return Vec2(std::cos(radians), std::sin(radians));
}

// Rotate vector by angle (radians)
inline Vec2 rotate(const Vec2& v, float radians) {
    float c = std::cos(radians);
    float s = std::sin(radians);
    return Vec2(x(v) * c - y(v) * s, x(v) * s + y(v) * c);
}

// Rotate vector around pivot point
inline Vec2 rotate_around(const Vec2& v, const Vec2& pivot, float radians) {
    return add(rotate(sub(v, pivot), radians), pivot);
}

// ============================================================================
// Angle Utilities
// ============================================================================

inline float degrees_to_radians(float degrees) {
    return degrees * (PI / 180.0f);
}

inline float radians_to_degrees(float radians) {
    return radians * (180.0f / PI);
}

// Normalize angle to [-PI, PI]
inline float normalize_angle(float radians) {
    while (radians > PI) radians -= TWO_PI;
    while (radians < -PI) radians += TWO_PI;
    return radians;
}

// Normalize angle to [0, TWO_PI]
inline float normalize_angle_positive(float radians) {
    while (radians >= TWO_PI) radians -= TWO_PI;
    while (radians < 0.0f) radians += TWO_PI;
    return radians;
}

// Lerp between angles (handles wrapping)
inline float lerp_angle(float a, float b, float t) {
    float diff = normalize_angle(b - a);
    return a + diff * t;
}

// ============================================================================
// Scalar Utilities
// ============================================================================

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline float inverse_lerp(float a, float b, float value) {
    if (std::abs(b - a) < EPSILON) return 0.0f;
    return (value - a) / (b - a);
}

inline float remap(float value, float from_min, float from_max, float to_min, float to_max) {
    float t = inverse_lerp(from_min, from_max, value);
    return lerp(to_min, to_max, t);
}

inline float clamp(float value, float min_val, float max_val) {
    return std::clamp(value, min_val, max_val);
}

inline float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline float sign(float value) {
    if (value > 0.0f) return 1.0f;
    if (value < 0.0f) return -1.0f;
    return 0.0f;
}

inline bool approximately(float a, float b, float epsilon = EPSILON) {
    return std::abs(a - b) < epsilon;
}

inline bool approximately(const Vec2& a, const Vec2& b, float epsilon = EPSILON) {
    return approximately(x(a), x(b), epsilon) && approximately(y(a), y(b), epsilon);
}

// ============================================================================
// Box Operations
// ============================================================================

inline Box make_box(const Vec2& min_corner, const Vec2& max_corner) {
    return Box(min_corner, max_corner);
}

inline Box make_box(float x, float y, float width, float height) {
    return Box(Vec2(x, y), Vec2(x + width, y + height));
}

inline Box make_box_centered(const Vec2& center, const Vec2& size) {
    Vec2 half = div(size, 2.0f);
    return Box(sub(center, half), add(center, half));
}

inline Vec2 box_min(const Box& b) { return b.min_corner(); }
inline Vec2 box_max(const Box& b) { return b.max_corner(); }

inline Vec2 box_size(const Box& b) {
    return sub(b.max_corner(), b.min_corner());
}

inline Vec2 box_center(const Box& b) {
    return mul(add(b.min_corner(), b.max_corner()), 0.5f);
}

inline float box_width(const Box& b) {
    return x(b.max_corner()) - x(b.min_corner());
}

inline float box_height(const Box& b) {
    return y(b.max_corner()) - y(b.min_corner());
}

inline float box_area(const Box& b) {
    return bg::area(b);
}

inline bool box_contains(const Box& b, const Vec2& p) {
    return bg::within(p, b);
}

inline bool box_intersects(const Box& a, const Box& b) {
    return bg::intersects(a, b);
}

inline Box box_expand(const Box& b, float amount) {
    return Box(
        sub(b.min_corner(), vec2(amount)),
        add(b.max_corner(), vec2(amount))
    );
}

inline Box box_expand(const Box& b, const Vec2& amount) {
    return Box(sub(b.min_corner(), amount), add(b.max_corner(), amount));
}

inline Box box_union(const Box& a, const Box& b) {
    return Box(
        min(a.min_corner(), b.min_corner()),
        max(a.max_corner(), b.max_corner())
    );
}

// ============================================================================
// Geometry Algorithms (Boost.Geometry wrappers)
// ============================================================================

// Distance from point to segment
inline float distance_to_segment(const Vec2& point, const Vec2& seg_start, const Vec2& seg_end) {
    Segment seg(seg_start, seg_end);
    return static_cast<float>(bg::distance(point, seg));
}

// Distance from point to box
inline float distance_to_box(const Vec2& point, const Box& box) {
    return static_cast<float>(bg::distance(point, box));
}

// Closest point on segment to a point
inline Vec2 closest_point_on_segment(const Vec2& point, const Vec2& seg_start, const Vec2& seg_end) {
    Vec2 seg = sub(seg_end, seg_start);
    float len_sq = length_squared(seg);
    if (len_sq < EPSILON) return seg_start;

    float t = clamp01(dot(sub(point, seg_start), seg) / len_sq);
    return add(seg_start, mul(seg, t));
}

// Closest point on box to a point
inline Vec2 closest_point_on_box(const Vec2& point, const Box& box) {
    return Vec2(
        clamp(x(point), x(box.min_corner()), x(box.max_corner())),
        clamp(y(point), y(box.min_corner()), y(box.max_corner()))
    );
}

// Line-line intersection
inline bool line_intersection(
    const Vec2& p1, const Vec2& p2,
    const Vec2& p3, const Vec2& p4,
    Vec2* out_point = nullptr,
    float* out_t1 = nullptr,
    float* out_t2 = nullptr)
{
    Vec2 d1 = sub(p2, p1);
    Vec2 d2 = sub(p4, p3);

    float denom = cross(d1, d2);
    if (std::abs(denom) < EPSILON) return false;  // Parallel

    Vec2 d3 = sub(p1, p3);
    float t1 = cross(d2, d3) / denom;
    float t2 = cross(d1, d3) / denom;

    if (out_point) *out_point = add(p1, mul(d1, t1));
    if (out_t1) *out_t1 = t1;
    if (out_t2) *out_t2 = t2;

    return true;
}

// Segment-segment intersection
inline bool segment_intersection(
    const Vec2& p1, const Vec2& p2,
    const Vec2& p3, const Vec2& p4,
    Vec2* out_point = nullptr)
{
    float t1, t2;
    if (!line_intersection(p1, p2, p3, p4, out_point, &t1, &t2)) {
        return false;
    }
    return t1 >= 0.0f && t1 <= 1.0f && t2 >= 0.0f && t2 <= 1.0f;
}

// Ray-box intersection
inline bool ray_box_intersection(
    const Vec2& origin,
    const Vec2& direction,
    const Box& box,
    float* out_t_near = nullptr,
    float* out_t_far = nullptr)
{
    Vec2 inv_dir = Vec2(1.0f / x(direction), 1.0f / y(direction));

    Vec2 t1 = mul(sub(box.min_corner(), origin), inv_dir);
    Vec2 t2 = mul(sub(box.max_corner(), origin), inv_dir);

    Vec2 t_min_v = min(t1, t2);
    Vec2 t_max_v = max(t1, t2);

    float t_near = std::max(x(t_min_v), y(t_min_v));
    float t_far = std::min(x(t_max_v), y(t_max_v));

    if (out_t_near) *out_t_near = t_near;
    if (out_t_far) *out_t_far = t_far;

    return t_near <= t_far && t_far >= 0.0f;
}

// ============================================================================
// Circle Operations
// ============================================================================

inline bool circle_contains(const Vec2& center, float radius, const Vec2& point) {
    return distance_squared(center, point) <= radius * radius;
}

inline bool circle_intersects_circle(
    const Vec2& center1, float radius1,
    const Vec2& center2, float radius2)
{
    float r_sum = radius1 + radius2;
    return distance_squared(center1, center2) <= r_sum * r_sum;
}

inline bool circle_intersects_box(const Vec2& center, float radius, const Box& box) {
    Vec2 closest = closest_point_on_box(center, box);
    return distance_squared(center, closest) <= radius * radius;
}

// ============================================================================
// Polygon Operations
// ============================================================================

inline float polygon_area(const Polygon& poly) {
    return static_cast<float>(bg::area(poly));
}

inline Vec2 polygon_centroid(const Polygon& poly) {
    Vec2 result;
    bg::centroid(poly, result);
    return result;
}

inline bool polygon_contains(const Polygon& poly, const Vec2& point) {
    return bg::within(point, poly);
}

inline float polygon_perimeter(const Polygon& poly) {
    return static_cast<float>(bg::perimeter(poly));
}

inline bool polygon_is_valid(const Polygon& poly) {
    return bg::is_valid(poly);
}

inline std::size_t polygon_num_points(const Polygon& poly) {
    return bg::num_points(poly);
}

// Fix polygon winding order and closure
inline void polygon_correct(Polygon& poly) {
    bg::correct(poly);
}

// Reverse point order
inline void polygon_reverse(Polygon& poly) {
    bg::reverse(poly);
}

// Simplify polygon (Douglas-Peucker)
inline Polygon polygon_simplify(const Polygon& poly, float max_distance) {
    Polygon result;
    bg::simplify(poly, result, max_distance);
    return result;
}

// Convex hull of polygon
inline Polygon polygon_convex_hull(const Polygon& poly) {
    Polygon result;
    bg::convex_hull(poly, result);
    return result;
}

// Bounding box of polygon
inline Box polygon_envelope(const Polygon& poly) {
    return bg::return_envelope<Box>(poly);
}

// Polygon construction helper
inline Polygon make_polygon(std::initializer_list<Vec2> points) {
    Polygon poly;
    for (const auto& p : points) {
        bg::append(poly.outer(), p);
    }
    bg::correct(poly);
    return poly;
}

// ============================================================================
// Polygon Boolean Operations
// ============================================================================

inline MultiPolygon polygon_union(const Polygon& a, const Polygon& b) {
    MultiPolygon result;
    bg::union_(a, b, result);
    return result;
}

inline MultiPolygon polygon_difference(const Polygon& a, const Polygon& b) {
    MultiPolygon result;
    bg::difference(a, b, result);
    return result;
}

inline MultiPolygon polygon_sym_difference(const Polygon& a, const Polygon& b) {
    MultiPolygon result;
    bg::sym_difference(a, b, result);
    return result;
}

inline MultiPolygon polygon_intersection(const Polygon& a, const Polygon& b) {
    MultiPolygon result;
    bg::intersection(a, b, result);
    return result;
}

// ============================================================================
// Spatial Predicates (generic templates)
// ============================================================================

template<typename Geom1, typename Geom2>
inline bool geom_disjoint(const Geom1& a, const Geom2& b) {
    return bg::disjoint(a, b);
}

template<typename Geom1, typename Geom2>
inline bool geom_intersects(const Geom1& a, const Geom2& b) {
    return bg::intersects(a, b);
}

template<typename Geom1, typename Geom2>
inline bool geom_within(const Geom1& a, const Geom2& b) {
    return bg::within(a, b);
}

template<typename Geom1, typename Geom2>
inline bool geom_covered_by(const Geom1& a, const Geom2& b) {
    return bg::covered_by(a, b);
}

template<typename Geom1, typename Geom2>
inline bool geom_touches(const Geom1& a, const Geom2& b) {
    return bg::touches(a, b);
}

template<typename Geom1, typename Geom2>
inline bool geom_equals(const Geom1& a, const Geom2& b) {
    return bg::equals(a, b);
}

template<typename Geom1, typename Geom2>
inline float geom_distance(const Geom1& a, const Geom2& b) {
    return static_cast<float>(bg::distance(a, b));
}

// ============================================================================
// Box Additional Operations
// ============================================================================

// Compute the actual intersection box (not just boolean test)
inline bool box_intersection(const Box& a, const Box& b, Box* out) {
    if (!bg::intersects(a, b)) return false;
    *out = Box(
        max(a.min_corner(), b.min_corner()),
        min(a.max_corner(), b.max_corner())
    );
    return true;
}

inline bool box_contains_box(const Box& outer, const Box& inner) {
    return bg::within(inner, outer);
}

inline float box_perimeter(const Box& b) {
    return 2.0f * (box_width(b) + box_height(b));
}

inline bool box_is_empty(const Box& b) {
    return box_width(b) <= 0.0f || box_height(b) <= 0.0f;
}

// Move box by offset
inline Box box_translate(const Box& b, const Vec2& offset) {
    return Box(add(b.min_corner(), offset), add(b.max_corner(), offset));
}

// Scale box from its center
inline Box box_scale(const Box& b, float sx, float sy) {
    Vec2 center = box_center(b);
    Vec2 half = mul(box_size(b), 0.5f);
    Vec2 scaled_half = Vec2(x(half) * sx, y(half) * sy);
    return Box(sub(center, scaled_half), add(center, scaled_half));
}

// Clamp point inside box
inline Vec2 box_clamp_point(const Box& b, const Vec2& p) {
    return closest_point_on_box(p, b);
}

// ============================================================================
// LineString Operations
// ============================================================================

inline LineString make_linestring(std::initializer_list<Vec2> points) {
    LineString ls;
    for (const auto& p : points) {
        bg::append(ls, p);
    }
    return ls;
}

inline float linestring_length(const LineString& ls) {
    return static_cast<float>(bg::length(ls));
}

inline std::size_t linestring_num_points(const LineString& ls) {
    return bg::num_points(ls);
}

inline Box linestring_envelope(const LineString& ls) {
    return bg::return_envelope<Box>(ls);
}

inline LineString linestring_simplify(const LineString& ls, float max_distance) {
    LineString result;
    bg::simplify(ls, result, max_distance);
    return result;
}

inline LineString linestring_reverse(const LineString& ls) {
    LineString result = ls;
    bg::reverse(result);
    return result;
}

// Point at parameter t (0..1) along linestring
inline Vec2 linestring_interpolate(const LineString& ls, float t) {
    if (ls.size() < 2) return ls.empty() ? vec2_zero() : ls[0];
    float total = linestring_length(ls);
    float target = total * clamp01(t);
    float accumulated = 0.0f;
    for (std::size_t i = 0; i + 1 < ls.size(); ++i) {
        float seg_len = distance(ls[i], ls[i + 1]);
        if (accumulated + seg_len >= target) {
            float seg_t = (seg_len > EPSILON) ? (target - accumulated) / seg_len : 0.0f;
            return lerp(ls[i], ls[i + 1], seg_t);
        }
        accumulated += seg_len;
    }
    return ls.back();
}

// ============================================================================
// MultiPoint Operations
// ============================================================================

inline MultiPoint make_multi_point(std::initializer_list<Vec2> points) {
    MultiPoint mp;
    for (const auto& p : points) {
        bg::append(mp, p);
    }
    return mp;
}

inline Box multi_point_envelope(const MultiPoint& mp) {
    return bg::return_envelope<Box>(mp);
}

inline Polygon multi_point_convex_hull(const MultiPoint& mp) {
    Polygon result;
    bg::convex_hull(mp, result);
    return result;
}

inline Vec2 multi_point_centroid(const MultiPoint& mp) {
    Vec2 result;
    bg::centroid(mp, result);
    return result;
}

// ============================================================================
// MultiPolygon Operations
// ============================================================================

inline float multi_polygon_area(const MultiPolygon& mp) {
    return static_cast<float>(bg::area(mp));
}

inline float multi_polygon_perimeter(const MultiPolygon& mp) {
    return static_cast<float>(bg::perimeter(mp));
}

inline Box multi_polygon_envelope(const MultiPolygon& mp) {
    return bg::return_envelope<Box>(mp);
}

inline Vec2 multi_polygon_centroid(const MultiPolygon& mp) {
    Vec2 result;
    bg::centroid(mp, result);
    return result;
}

inline bool multi_polygon_contains(const MultiPolygon& mp, const Vec2& point) {
    return bg::within(point, mp);
}

// ============================================================================
// Ring Operations
// ============================================================================

inline Ring make_ring(std::initializer_list<Vec2> points) {
    Ring r;
    for (const auto& p : points) {
        bg::append(r, p);
    }
    bg::correct(r);
    return r;
}

inline float ring_area(const Ring& r) {
    return static_cast<float>(bg::area(r));
}

inline float ring_perimeter(const Ring& r) {
    return static_cast<float>(bg::perimeter(r));
}

inline Vec2 ring_centroid(const Ring& r) {
    Vec2 result;
    bg::centroid(r, result);
    return result;
}

inline bool ring_contains(const Ring& r, const Vec2& point) {
    return bg::within(point, r);
}

inline Box ring_envelope(const Ring& r) {
    return bg::return_envelope<Box>(r);
}

// ============================================================================
// Segment Operations
// ============================================================================

inline Segment make_segment(const Vec2& a, const Vec2& b) {
    return Segment(a, b);
}

inline float segment_length(const Segment& s) {
    return distance(s.first, s.second);
}

inline Vec2 segment_midpoint(const Segment& s) {
    return lerp(s.first, s.second, 0.5f);
}

inline Vec2 segment_direction(const Segment& s) {
    return normalize(sub(s.second, s.first));
}

inline Vec2 segment_interpolate(const Segment& s, float t) {
    return lerp(s.first, s.second, t);
}

// ============================================================================
// Envelope (bounding box) for any geometry
// ============================================================================

template<typename Geometry>
inline Box envelope(const Geometry& geom) {
    return bg::return_envelope<Box>(geom);
}

// ============================================================================
// Transform2D - 2D Affine Transformation Matrix
// ============================================================================
//
//  | m[0]  m[1]  m[2] |     | sx*cos  -sy*sin  tx |
//  | m[3]  m[4]  m[5] |  =  | sx*sin   sy*cos  ty |
//  | 0     0     1    |     | 0        0        1  |
//

struct Transform2D {
    float m[6]; // row-major: [a, b, tx, c, d, ty]

    Transform2D() : m{1, 0, 0, 0, 1, 0} {} // identity

    Transform2D(float a, float b, float tx, float c, float d, float ty)
        : m{a, b, tx, c, d, ty} {}

    static Transform2D identity() {
        return Transform2D();
    }

    static Transform2D translate(float tx, float ty) {
        return Transform2D(1, 0, tx, 0, 1, ty);
    }

    static Transform2D translate(const Vec2& t) {
        return translate(x(t), y(t));
    }

    static Transform2D rotate(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Transform2D(c, -s, 0, s, c, 0);
    }

    static Transform2D rotate_around(float radians, const Vec2& pivot) {
        return translate(pivot) * rotate(radians) * translate(neg(pivot));
    }

    static Transform2D scale(float sx, float sy) {
        return Transform2D(sx, 0, 0, 0, sy, 0);
    }

    static Transform2D scale(float s) {
        return scale(s, s);
    }

    static Transform2D scale(const Vec2& s) {
        return scale(x(s), y(s));
    }

    static Transform2D scale_around(float sx, float sy, const Vec2& pivot) {
        return translate(pivot) * scale(sx, sy) * translate(neg(pivot));
    }

    static Transform2D shear(float sx, float sy) {
        return Transform2D(1, sx, 0, sy, 1, 0);
    }

    // Matrix multiplication: this * other
    Transform2D operator*(const Transform2D& o) const {
        return Transform2D(
            m[0]*o.m[0] + m[1]*o.m[3],
            m[0]*o.m[1] + m[1]*o.m[4],
            m[0]*o.m[2] + m[1]*o.m[5] + m[2],
            m[3]*o.m[0] + m[4]*o.m[3],
            m[3]*o.m[1] + m[4]*o.m[4],
            m[3]*o.m[2] + m[4]*o.m[5] + m[5]
        );
    }

    Transform2D& operator*=(const Transform2D& o) {
        *this = *this * o;
        return *this;
    }

    // Apply to point
    Vec2 apply(const Vec2& p) const {
        return Vec2(
            m[0] * x(p) + m[1] * y(p) + m[2],
            m[3] * x(p) + m[4] * y(p) + m[5]
        );
    }

    Vec2 operator*(const Vec2& p) const { return apply(p); }

    // Apply to direction (no translation)
    Vec2 apply_direction(const Vec2& d) const {
        return Vec2(
            m[0] * x(d) + m[1] * y(d),
            m[3] * x(d) + m[4] * y(d)
        );
    }

    // Apply to box (returns AABB of transformed corners)
    Box apply(const Box& b) const {
        Vec2 corners[4] = {
            apply(b.min_corner()),
            apply(Vec2(x(b.max_corner()), y(b.min_corner()))),
            apply(b.max_corner()),
            apply(Vec2(x(b.min_corner()), y(b.max_corner())))
        };
        Vec2 mn = corners[0], mx = corners[0];
        for (int i = 1; i < 4; ++i) {
            mn = min(mn, corners[i]);
            mx = max(mx, corners[i]);
        }
        return Box(mn, mx);
    }

    // Apply to polygon
    Polygon apply(const Polygon& poly) const {
        Polygon result;
        for (const auto& p : poly.outer()) {
            bg::append(result.outer(), apply(p));
        }
        for (const auto& inner : poly.inners()) {
            result.inners().push_back({});
            auto& new_inner = result.inners().back();
            for (const auto& p : inner) {
                new_inner.push_back(apply(p));
            }
        }
        return result;
    }

    // Apply to linestring
    LineString apply(const LineString& ls) const {
        LineString result;
        for (const auto& p : ls) {
            bg::append(result, apply(p));
        }
        return result;
    }

    // Apply to ring
    Ring apply(const Ring& r) const {
        Ring result;
        for (const auto& p : r) {
            result.push_back(apply(p));
        }
        return result;
    }

    // Determinant
    float determinant() const {
        return m[0] * m[4] - m[1] * m[3];
    }

    // Inverse (returns identity if singular)
    Transform2D inverse() const {
        float det = determinant();
        if (std::abs(det) < EPSILON) return identity();
        float inv_det = 1.0f / det;
        return Transform2D(
             m[4] * inv_det,
            -m[1] * inv_det,
            (m[1] * m[5] - m[4] * m[2]) * inv_det,
            -m[3] * inv_det,
             m[0] * inv_det,
            (m[3] * m[2] - m[0] * m[5]) * inv_det
        );
    }

    // Extract translation
    Vec2 get_translation() const { return Vec2(m[2], m[5]); }

    // Extract scale (approximate, assumes no shear)
    Vec2 get_scale() const {
        return Vec2(
            length(Vec2(m[0], m[3])),
            length(Vec2(m[1], m[4]))
        );
    }

    // Extract rotation angle (approximate, assumes uniform scale, no shear)
    float get_rotation() const {
        return std::atan2(m[3], m[0]);
    }
};

// ############################################################################
//
//  3D MATH TYPES
//
// ############################################################################

// ============================================================================
// Vec3
// ============================================================================

struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3(float v) : x(v), y(v), z(v) {}
    Vec3(const Vec2& v, float z_ = 0.0f) : x(math::x(v)), y(math::y(v)), z(z_) {}

    float& operator[](int i) { return (&x)[i]; }
    float  operator[](int i) const { return (&x)[i]; }

    Vec2 xy() const { return Vec2(x, y); }
    Vec2 xz() const { return Vec2(x, z); }
    Vec2 yz() const { return Vec2(y, z); }

    static Vec3 zero()    { return Vec3(0, 0, 0); }
    static Vec3 one()     { return Vec3(1, 1, 1); }
    static Vec3 unit_x()  { return Vec3(1, 0, 0); }
    static Vec3 unit_y()  { return Vec3(0, 1, 0); }
    static Vec3 unit_z()  { return Vec3(0, 0, 1); }
    static Vec3 up()      { return Vec3(0, 1, 0); }
    static Vec3 down()    { return Vec3(0, -1, 0); }
    static Vec3 left()    { return Vec3(-1, 0, 0); }
    static Vec3 right()   { return Vec3(1, 0, 0); }
    static Vec3 forward() { return Vec3(0, 0, -1); }
    static Vec3 back()    { return Vec3(0, 0, 1); }
};

// Arithmetic
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator*(const Vec3& v, float s)        { return {v.x*s, v.y*s, v.z*s}; }
inline Vec3 operator*(float s, const Vec3& v)        { return {v.x*s, v.y*s, v.z*s}; }
inline Vec3 operator*(const Vec3& a, const Vec3& b)  { return {a.x*b.x, a.y*b.y, a.z*b.z}; }
inline Vec3 operator/(const Vec3& v, float s)        { return {v.x/s, v.y/s, v.z/s}; }
inline Vec3 operator/(const Vec3& a, const Vec3& b)  { return {a.x/b.x, a.y/b.y, a.z/b.z}; }
inline Vec3 operator-(const Vec3& v)                 { return {-v.x, -v.y, -v.z}; }

inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x+=b.x; a.y+=b.y; a.z+=b.z; return a; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { a.x-=b.x; a.y-=b.y; a.z-=b.z; return a; }
inline Vec3& operator*=(Vec3& v, float s)       { v.x*=s; v.y*=s; v.z*=s; return v; }
inline Vec3& operator/=(Vec3& v, float s)       { v.x/=s; v.y/=s; v.z/=s; return v; }
inline Vec3& operator*=(Vec3& a, const Vec3& b) { a.x*=b.x; a.y*=b.y; a.z*=b.z; return a; }

inline bool operator==(const Vec3& a, const Vec3& b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
inline bool operator!=(const Vec3& a, const Vec3& b) { return !(a == b); }

// Vector math
#if defined(WINDOW_MATH_SIMD)
inline float dot(const Vec3& a, const Vec3& b) {
    return simd::dot3(simd::load3(&a.x), simd::load3(&b.x));
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    simd::f128 r = simd::cross3(simd::load3(&a.x), simd::load3(&b.x));
    Vec3 out;
    alignas(16) float tmp[4];
    simd::store4a(tmp, r);
    out.x = tmp[0]; out.y = tmp[1]; out.z = tmp[2];
    return out;
}
#else
inline float dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
#endif

inline float length_squared(const Vec3& v) { return dot(v, v); }
inline float length(const Vec3& v)         { return std::sqrt(length_squared(v)); }

inline float distance(const Vec3& a, const Vec3& b)         { return length(b - a); }
inline float distance_squared(const Vec3& a, const Vec3& b) { return length_squared(b - a); }

inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    return (len < EPSILON) ? Vec3::zero() : v / len;
}

inline Vec3 normalize_or(const Vec3& v, const Vec3& fallback) {
    float len = length(v);
    return (len < EPSILON) ? fallback : v / len;
}

inline Vec3 reflect(const Vec3& v, const Vec3& n) { return v - n * (2.0f * dot(v, n)); }

inline Vec3 project(const Vec3& a, const Vec3& b) {
    float len_sq = length_squared(b);
    return (len_sq < EPSILON) ? Vec3::zero() : b * (dot(a, b) / len_sq);
}

inline Vec3 reject(const Vec3& a, const Vec3& b) { return a - project(a, b); }

// Refract (eta = ratio of indices of refraction)
inline Vec3 refract(const Vec3& incident, const Vec3& normal, float eta) {
    float d = dot(normal, incident);
    float k = 1.0f - eta * eta * (1.0f - d * d);
    if (k < 0.0f) return Vec3::zero();
    return incident * eta - normal * (eta * d + std::sqrt(k));
}

// Interpolation
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

inline Vec3 smoothstep(const Vec3& a, const Vec3& b, float t) {
    float s = smoothstep(0.0f, 1.0f, t);
    return lerp(a, b, s);
}

// Min / max / clamp
inline Vec3 min(const Vec3& a, const Vec3& b) { return {std::min(a.x,b.x), std::min(a.y,b.y), std::min(a.z,b.z)}; }
inline Vec3 max(const Vec3& a, const Vec3& b) { return {std::max(a.x,b.x), std::max(a.y,b.y), std::max(a.z,b.z)}; }

inline Vec3 clamp(const Vec3& v, const Vec3& lo, const Vec3& hi) { return max(lo, min(v, hi)); }

inline Vec3 clamp_length(const Vec3& v, float max_len) {
    float len = length(v);
    return (len > max_len && len > EPSILON) ? v * (max_len / len) : v;
}

inline Vec3 abs(const Vec3& v)   { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }
inline Vec3 floor(const Vec3& v) { return {std::floor(v.x), std::floor(v.y), std::floor(v.z)}; }
inline Vec3 ceil(const Vec3& v)  { return {std::ceil(v.x), std::ceil(v.y), std::ceil(v.z)}; }
inline Vec3 round(const Vec3& v) { return {std::round(v.x), std::round(v.y), std::round(v.z)}; }

inline float min_component(const Vec3& v) { return std::min({v.x, v.y, v.z}); }
inline float max_component(const Vec3& v) { return std::max({v.x, v.y, v.z}); }

inline bool approximately(const Vec3& a, const Vec3& b, float eps = EPSILON) {
    return approximately(a.x, b.x, eps) && approximately(a.y, b.y, eps) && approximately(a.z, b.z, eps);
}

// ============================================================================
// Vec4
// ============================================================================

struct alignas(16) Vec4 {
    union {
        struct { float x, y, z, w; };
        float data[4];
#if defined(WINDOW_MATH_SSE)
        __m128 simd;
#elif defined(WINDOW_MATH_NEON)
        float32x4_t simd;
#endif
    };

    Vec4() : data{0, 0, 0, 0} {}
    Vec4(float x_, float y_, float z_, float w_) : data{x_, y_, z_, w_} {}
    explicit Vec4(float v) : data{v, v, v, v} {}
    Vec4(const Vec3& v, float w_ = 0.0f) : data{v.x, v.y, v.z, w_} {}
    Vec4(const Vec2& v, float z_ = 0.0f, float w_ = 0.0f) : data{math::x(v), math::y(v), z_, w_} {}
#if defined(WINDOW_MATH_SIMD)
    Vec4(simd::f128 s) : simd(s) {}
#endif

    float& operator[](int i) { return data[i]; }
    float  operator[](int i) const { return data[i]; }

    Vec2 xy() const { return Vec2(x, y); }
    Vec3 xyz() const { return Vec3(x, y, z); }

    static Vec4 zero() { return {0,0,0,0}; }
    static Vec4 one()  { return {1,1,1,1}; }
};

#if defined(WINDOW_MATH_SIMD)
inline Vec4 operator+(const Vec4& a, const Vec4& b) { return Vec4(simd::add(a.simd, b.simd)); }
inline Vec4 operator-(const Vec4& a, const Vec4& b) { return Vec4(simd::sub(a.simd, b.simd)); }
inline Vec4 operator*(const Vec4& v, float s)        { return Vec4(simd::mul(v.simd, simd::splat(s))); }
inline Vec4 operator*(float s, const Vec4& v)        { return Vec4(simd::mul(v.simd, simd::splat(s))); }
inline Vec4 operator*(const Vec4& a, const Vec4& b)  { return Vec4(simd::mul(a.simd, b.simd)); }
inline Vec4 operator/(const Vec4& v, float s)        { return Vec4(simd::div(v.simd, simd::splat(s))); }
inline Vec4 operator/(const Vec4& a, const Vec4& b)  { return Vec4(simd::div(a.simd, b.simd)); }
inline Vec4 operator-(const Vec4& v)                 { return Vec4(simd::neg(v.simd)); }

inline Vec4& operator+=(Vec4& a, const Vec4& b) { a.simd = simd::add(a.simd, b.simd); return a; }
inline Vec4& operator-=(Vec4& a, const Vec4& b) { a.simd = simd::sub(a.simd, b.simd); return a; }
inline Vec4& operator*=(Vec4& v, float s)       { v.simd = simd::mul(v.simd, simd::splat(s)); return v; }
inline Vec4& operator/=(Vec4& v, float s)       { v.simd = simd::div(v.simd, simd::splat(s)); return v; }

inline float dot(const Vec4& a, const Vec4& b) { return simd::dot4(a.simd, b.simd); }

inline Vec4 min(const Vec4& a, const Vec4& b) { return Vec4(simd::vmin(a.simd, b.simd)); }
inline Vec4 max(const Vec4& a, const Vec4& b) { return Vec4(simd::vmax(a.simd, b.simd)); }
inline Vec4 abs(const Vec4& v)                { return Vec4(simd::vabs(v.simd)); }
#else
inline Vec4 operator+(const Vec4& a, const Vec4& b) { return {a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w}; }
inline Vec4 operator-(const Vec4& a, const Vec4& b) { return {a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w}; }
inline Vec4 operator*(const Vec4& v, float s)        { return {v.x*s, v.y*s, v.z*s, v.w*s}; }
inline Vec4 operator*(float s, const Vec4& v)        { return {v.x*s, v.y*s, v.z*s, v.w*s}; }
inline Vec4 operator*(const Vec4& a, const Vec4& b)  { return {a.x*b.x, a.y*b.y, a.z*b.z, a.w*b.w}; }
inline Vec4 operator/(const Vec4& v, float s)        { return {v.x/s, v.y/s, v.z/s, v.w/s}; }
inline Vec4 operator/(const Vec4& a, const Vec4& b)  { return {a.x/b.x, a.y/b.y, a.z/b.z, a.w/b.w}; }
inline Vec4 operator-(const Vec4& v)                 { return {-v.x, -v.y, -v.z, -v.w}; }

inline Vec4& operator+=(Vec4& a, const Vec4& b) { a.x+=b.x; a.y+=b.y; a.z+=b.z; a.w+=b.w; return a; }
inline Vec4& operator-=(Vec4& a, const Vec4& b) { a.x-=b.x; a.y-=b.y; a.z-=b.z; a.w-=b.w; return a; }
inline Vec4& operator*=(Vec4& v, float s)       { v.x*=s; v.y*=s; v.z*=s; v.w*=s; return v; }
inline Vec4& operator/=(Vec4& v, float s)       { v.x/=s; v.y/=s; v.z/=s; v.w/=s; return v; }

inline float dot(const Vec4& a, const Vec4& b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }

inline Vec4 min(const Vec4& a, const Vec4& b) { return {std::min(a.x,b.x), std::min(a.y,b.y), std::min(a.z,b.z), std::min(a.w,b.w)}; }
inline Vec4 max(const Vec4& a, const Vec4& b) { return {std::max(a.x,b.x), std::max(a.y,b.y), std::max(a.z,b.z), std::max(a.w,b.w)}; }
inline Vec4 abs(const Vec4& v) { return {std::abs(v.x), std::abs(v.y), std::abs(v.z), std::abs(v.w)}; }
#endif

inline bool operator==(const Vec4& a, const Vec4& b) { return a.x==b.x && a.y==b.y && a.z==b.z && a.w==b.w; }
inline bool operator!=(const Vec4& a, const Vec4& b) { return !(a == b); }

inline float length_squared(const Vec4& v) { return dot(v, v); }
inline float length(const Vec4& v)         { return std::sqrt(length_squared(v)); }

inline Vec4 normalize(const Vec4& v) {
    float len = length(v);
    return (len < EPSILON) ? Vec4::zero() : v / len;
}

inline Vec4 lerp(const Vec4& a, const Vec4& b, float t) { return a + (b - a) * t; }

inline bool approximately(const Vec4& a, const Vec4& b, float eps = EPSILON) {
    return approximately(a.x,b.x,eps) && approximately(a.y,b.y,eps) && approximately(a.z,b.z,eps) && approximately(a.w,b.w,eps);
}

// ============================================================================
// Forward declarations for Quaternion / Matrix interop
// ============================================================================

struct Quat;
struct Mat3;
struct Mat4;

// ============================================================================
// Quat (Quaternion) - stored as Vec4 {x, y, z, w} where w is scalar part
// ============================================================================

struct Quat {
    Vec4 v; // {x, y, z, w}

    Quat() : v(0, 0, 0, 1) {}
    Quat(float x_, float y_, float z_, float w_) : v(x_, y_, z_, w_) {}
    explicit Quat(const Vec4& v_) : v(v_) {}

    float& x() { return v.x; }
    float& y() { return v.y; }
    float& z() { return v.z; }
    float& w() { return v.w; }
    float x() const { return v.x; }
    float y() const { return v.y; }
    float z() const { return v.z; }
    float w() const { return v.w; }

    static Quat identity() { return Quat(0, 0, 0, 1); }

    static Quat from_axis_angle(const Vec3& axis, float radians) {
        Vec3 a = normalize(axis);
        float half = radians * 0.5f;
        float s = std::sin(half);
        return Quat(a.x*s, a.y*s, a.z*s, std::cos(half));
    }

    // Euler angles (radians) in YXZ order (yaw, pitch, roll)
    static Quat from_euler(float pitch, float yaw, float roll) {
        float cy = std::cos(yaw * 0.5f),   sy = std::sin(yaw * 0.5f);
        float cp = std::cos(pitch * 0.5f), sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f),  sr = std::sin(roll * 0.5f);
        return Quat(
            cy*sp*cr + sy*cp*sr,
            sy*cp*cr - cy*sp*sr,
            cy*cp*sr - sy*sp*cr,
            cy*cp*cr + sy*sp*sr
        );
    }

    static Quat from_euler(const Vec3& euler) { return from_euler(euler.x, euler.y, euler.z); }

    // Build quaternion from rotation matrix (expects orthonormal upper-left 3x3)
    static Quat from_mat3(const Mat3& m);
    static Quat from_mat4(const Mat4& m);

    // Look rotation: builds quaternion that rotates Vec3::forward() to look at dir
    static Quat look_rotation(const Vec3& forward, const Vec3& up = Vec3::up());

    Vec3 euler() const {
        // Returns pitch, yaw, roll (x, y, z)
        float sinp = 2.0f * (v.w * v.x + v.y * v.z);
        float cosp = 1.0f - 2.0f * (v.x * v.x + v.y * v.y);
        float pitch = std::atan2(sinp, cosp);

        float siny = 2.0f * (v.w * v.y - v.z * v.x);
        float yaw = (std::abs(siny) >= 1.0f) ? std::copysign(HALF_PI, siny) : std::asin(siny);

        float sinr = 2.0f * (v.w * v.z + v.x * v.y);
        float cosr = 1.0f - 2.0f * (v.y * v.y + v.z * v.z);
        float roll = std::atan2(sinr, cosr);

        return {pitch, yaw, roll};
    }

    void to_axis_angle(Vec3* out_axis, float* out_angle) const {
        Quat q = normalized();
        float half = std::acos(std::clamp(q.w(), -1.0f, 1.0f));
        *out_angle = half * 2.0f;
        float s = std::sin(half);
        *out_axis = (s > EPSILON) ? Vec3(q.x()/s, q.y()/s, q.z()/s) : Vec3::unit_x();
    }

    Mat3 to_mat3() const;
    Mat4 to_mat4() const;

    float length_squared() const { return math::length_squared(v); }
    float length() const { return math::length(v); }

    Quat normalized() const {
        float len = length();
        return (len < EPSILON) ? identity() : Quat(v / len);
    }

    Quat conjugate() const { return Quat(-v.x, -v.y, -v.z, v.w); }

    Quat inverse() const {
        float len_sq = length_squared();
        if (len_sq < EPSILON) return identity();
        Quat c = conjugate();
        return Quat(c.v / len_sq);
    }

    // Rotate a vector by this quaternion
    Vec3 rotate(const Vec3& p) const {
        Vec3 u(v.x, v.y, v.z);
        float s = v.w;
        return u * 2.0f * dot(u, p) + p * (s*s - dot(u, u)) + cross(u, p) * 2.0f * s;
    }
};

#if defined(WINDOW_MATH_SSE)
inline Quat operator*(const Quat& a, const Quat& b) {
    // SSE quaternion multiply using shuffles
    __m128 a_v = a.v.simd;
    __m128 b_v = b.v.simd;

    // Broadcast each component of b
    __m128 bx = _mm_shuffle_ps(b_v, b_v, _MM_SHUFFLE(0, 0, 0, 0));
    __m128 by = _mm_shuffle_ps(b_v, b_v, _MM_SHUFFLE(1, 1, 1, 1));
    __m128 bz = _mm_shuffle_ps(b_v, b_v, _MM_SHUFFLE(2, 2, 2, 2));
    __m128 bw = _mm_shuffle_ps(b_v, b_v, _MM_SHUFFLE(3, 3, 3, 3));

    // Signs for quaternion multiplication
    static const __m128 sign_xyzw = _mm_set_ps( 1, -1,  1,  1);
    static const __m128 sign_yxwz = _mm_set_ps( 1,  1,  1, -1);
    static const __m128 sign_zwxy = _mm_set_ps( 1,  1, -1,  1);
    static const __m128 sign_wzyx = _mm_set_ps(-1,  1,  1,  1);

    // a shuffled for each b component
    // result = w*b.x*(w,z,-y,x) + ...
    __m128 r0 = _mm_mul_ps(_mm_mul_ps(_mm_shuffle_ps(a_v, a_v, _MM_SHUFFLE(0, 1, 2, 3)), bw), sign_xyzw);
    __m128 r1 = _mm_mul_ps(_mm_mul_ps(_mm_shuffle_ps(a_v, a_v, _MM_SHUFFLE(1, 0, 3, 2)), bz), sign_yxwz);
    __m128 r2 = _mm_mul_ps(_mm_mul_ps(_mm_shuffle_ps(a_v, a_v, _MM_SHUFFLE(2, 3, 0, 1)), by), sign_zwxy);
    __m128 r3 = _mm_mul_ps(_mm_mul_ps(a_v, bx), sign_wzyx);

    return Quat(Vec4(_mm_add_ps(_mm_add_ps(r0, r1), _mm_add_ps(r2, r3))));
}
#else
inline Quat operator*(const Quat& a, const Quat& b) {
    return Quat(
        a.w()*b.x() + a.x()*b.w() + a.y()*b.z() - a.z()*b.y(),
        a.w()*b.y() - a.x()*b.z() + a.y()*b.w() + a.z()*b.x(),
        a.w()*b.z() + a.x()*b.y() - a.y()*b.x() + a.z()*b.w(),
        a.w()*b.w() - a.x()*b.x() - a.y()*b.y() - a.z()*b.z()
    );
}
#endif

inline Quat& operator*=(Quat& a, const Quat& b) { a = a * b; return a; }

inline bool operator==(const Quat& a, const Quat& b) { return a.v == b.v; }
inline bool operator!=(const Quat& a, const Quat& b) { return !(a == b); }

inline float dot(const Quat& a, const Quat& b) { return dot(a.v, b.v); }

inline Quat normalize(const Quat& q) { return q.normalized(); }

inline Quat lerp(const Quat& a, const Quat& b, float t) {
    return Quat(math::lerp(a.v, b.v, t)).normalized();
}

inline Quat slerp(const Quat& a, const Quat& b, float t) {
    float d = dot(a, b);
    Quat b2 = (d < 0.0f) ? Quat(-b.v.x, -b.v.y, -b.v.z, -b.v.w) : b;
    d = std::abs(d);
    if (d > 0.9995f) return lerp(a, b2, t);
    float theta = std::acos(std::clamp(d, -1.0f, 1.0f));
    float sin_theta = std::sin(theta);
    float wa = std::sin((1.0f - t) * theta) / sin_theta;
    float wb = std::sin(t * theta) / sin_theta;
    return Quat(a.v * wa + b2.v * wb);
}

inline bool approximately(const Quat& a, const Quat& b, float eps = EPSILON) {
    return approximately(a.v, b.v, eps);
}

// Angle between two quaternions (radians)
inline float angle_between(const Quat& a, const Quat& b) {
    float d = std::abs(dot(a, b));
    return 2.0f * std::acos(std::clamp(d, 0.0f, 1.0f));
}

// ============================================================================
// Mat3 - 3x3 Column-Major Matrix
// ============================================================================
//
//  Storage: m[col][row]  (column-major, OpenGL convention)
//
//  | m[0][0]  m[1][0]  m[2][0] |
//  | m[0][1]  m[1][1]  m[2][1] |
//  | m[0][2]  m[1][2]  m[2][2] |
//

struct alignas(16) Mat3 {
    float m[3][3]; // m[column][row]

    Mat3() : m{{1,0,0},{0,1,0},{0,0,1}} {}

    Mat3(float m00, float m10, float m20,
         float m01, float m11, float m21,
         float m02, float m12, float m22)
        : m{{m00,m01,m02},{m10,m11,m12},{m20,m21,m22}} {}

    // Construct from column vectors
    Mat3(const Vec3& c0, const Vec3& c1, const Vec3& c2)
        : m{{c0.x,c0.y,c0.z},{c1.x,c1.y,c1.z},{c2.x,c2.y,c2.z}} {}

    float& operator()(int row, int col) { return m[col][row]; }
    float  operator()(int row, int col) const { return m[col][row]; }

    Vec3 column(int c) const { return {m[c][0], m[c][1], m[c][2]}; }
    Vec3 row(int r) const    { return {m[0][r], m[1][r], m[2][r]}; }

    void set_column(int c, const Vec3& v) { m[c][0]=v.x; m[c][1]=v.y; m[c][2]=v.z; }
    void set_row(int r, const Vec3& v)    { m[0][r]=v.x; m[1][r]=v.y; m[2][r]=v.z; }

    const float* data() const { return &m[0][0]; }
    float*       data()       { return &m[0][0]; }

    static Mat3 identity() { return Mat3(); }
    static Mat3 zero()     { Mat3 r; std::fill(&r.m[0][0], &r.m[0][0]+9, 0.0f); return r; }

    static Mat3 from_scale(float sx, float sy, float sz) {
        Mat3 r = zero();
        r.m[0][0]=sx; r.m[1][1]=sy; r.m[2][2]=sz;
        return r;
    }
    static Mat3 from_scale(const Vec3& s) { return from_scale(s.x, s.y, s.z); }
    static Mat3 from_scale(float s)       { return from_scale(s, s, s); }

    static Mat3 from_rotation_x(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Mat3({1,0,0},{0,c,s},{0,-s,c});
    }

    static Mat3 from_rotation_y(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Mat3({c,0,-s},{0,1,0},{s,0,c});
    }

    static Mat3 from_rotation_z(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Mat3({c,s,0},{-s,c,0},{0,0,1});
    }

    static Mat3 from_axis_angle(const Vec3& axis, float rad) {
        Vec3 a = normalize(axis);
        float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
        return Mat3(
            t*a.x*a.x + c,      t*a.y*a.x + s*a.z,  t*a.z*a.x - s*a.y,
            t*a.x*a.y - s*a.z,  t*a.y*a.y + c,       t*a.z*a.y + s*a.x,
            t*a.x*a.z + s*a.y,  t*a.y*a.z - s*a.x,   t*a.z*a.z + c
        );
    }

    float determinant() const {
        return m[0][0]*(m[1][1]*m[2][2] - m[2][1]*m[1][2])
             - m[1][0]*(m[0][1]*m[2][2] - m[2][1]*m[0][2])
             + m[2][0]*(m[0][1]*m[1][2] - m[1][1]*m[0][2]);
    }

    Mat3 transposed() const {
        return Mat3(
            m[0][0], m[0][1], m[0][2],
            m[1][0], m[1][1], m[1][2],
            m[2][0], m[2][1], m[2][2]
        );
    }

    Mat3 inverse() const {
        float det = determinant();
        if (std::abs(det) < EPSILON) return identity();
        float inv = 1.0f / det;
        Mat3 r;
        r.m[0][0] = (m[1][1]*m[2][2] - m[2][1]*m[1][2]) * inv;
        r.m[0][1] = (m[2][1]*m[0][2] - m[0][1]*m[2][2]) * inv;
        r.m[0][2] = (m[0][1]*m[1][2] - m[1][1]*m[0][2]) * inv;
        r.m[1][0] = (m[2][0]*m[1][2] - m[1][0]*m[2][2]) * inv;
        r.m[1][1] = (m[0][0]*m[2][2] - m[2][0]*m[0][2]) * inv;
        r.m[1][2] = (m[1][0]*m[0][2] - m[0][0]*m[1][2]) * inv;
        r.m[2][0] = (m[1][0]*m[2][1] - m[2][0]*m[1][1]) * inv;
        r.m[2][1] = (m[2][0]*m[0][1] - m[0][0]*m[2][1]) * inv;
        r.m[2][2] = (m[0][0]*m[1][1] - m[1][0]*m[0][1]) * inv;
        return r;
    }
};

inline Mat3 operator*(const Mat3& a, const Mat3& b) {
    Mat3 r = Mat3::zero();
    for (int c = 0; c < 3; ++c)
        for (int ri = 0; ri < 3; ++ri)
            for (int k = 0; k < 3; ++k)
                r.m[c][ri] += a.m[k][ri] * b.m[c][k];
    return r;
}

inline Vec3 operator*(const Mat3& m, const Vec3& v) {
    return {
        m.m[0][0]*v.x + m.m[1][0]*v.y + m.m[2][0]*v.z,
        m.m[0][1]*v.x + m.m[1][1]*v.y + m.m[2][1]*v.z,
        m.m[0][2]*v.x + m.m[1][2]*v.y + m.m[2][2]*v.z
    };
}

inline Mat3 operator*(const Mat3& m, float s) {
    Mat3 r;
    for (int i = 0; i < 9; ++i) (&r.m[0][0])[i] = (&m.m[0][0])[i] * s;
    return r;
}

inline Mat3 operator+(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 9; ++i) (&r.m[0][0])[i] = (&a.m[0][0])[i] + (&b.m[0][0])[i];
    return r;
}

inline Mat3 operator-(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 9; ++i) (&r.m[0][0])[i] = (&a.m[0][0])[i] - (&b.m[0][0])[i];
    return r;
}

// ============================================================================
// Mat4 - 4x4 Column-Major Matrix
// ============================================================================
//
//  Storage: m[col][row]  (column-major, OpenGL convention)
//
//  | m[0][0]  m[1][0]  m[2][0]  m[3][0] |
//  | m[0][1]  m[1][1]  m[2][1]  m[3][1] |
//  | m[0][2]  m[1][2]  m[2][2]  m[3][2] |
//  | m[0][3]  m[1][3]  m[2][3]  m[3][3] |
//

struct alignas(16) Mat4 {
    float m[4][4]; // m[column][row]

    Mat4() : m{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}} {}

    Mat4(float m00, float m10, float m20, float m30,
         float m01, float m11, float m21, float m31,
         float m02, float m12, float m22, float m32,
         float m03, float m13, float m23, float m33)
        : m{{m00,m01,m02,m03},{m10,m11,m12,m13},{m20,m21,m22,m23},{m30,m31,m32,m33}} {}

    // Construct from column vectors
    Mat4(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3)
        : m{{c0.x,c0.y,c0.z,c0.w},{c1.x,c1.y,c1.z,c1.w},{c2.x,c2.y,c2.z,c2.w},{c3.x,c3.y,c3.z,c3.w}} {}

    // Construct from Mat3 (upper-left 3x3, rest is identity)
    explicit Mat4(const Mat3& m3) : Mat4() {
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                m[c][r] = m3.m[c][r];
    }

    float& operator()(int row, int col) { return m[col][row]; }
    float  operator()(int row, int col) const { return m[col][row]; }

    Vec4 column(int c) const { return {m[c][0], m[c][1], m[c][2], m[c][3]}; }
    Vec4 row(int r) const    { return {m[0][r], m[1][r], m[2][r], m[3][r]}; }

    void set_column(int c, const Vec4& v) { m[c][0]=v.x; m[c][1]=v.y; m[c][2]=v.z; m[c][3]=v.w; }
    void set_row(int r, const Vec4& v)    { m[0][r]=v.x; m[1][r]=v.y; m[2][r]=v.z; m[3][r]=v.w; }

    Mat3 upper_left_3x3() const {
        return Mat3(
            {m[0][0], m[0][1], m[0][2]},
            {m[1][0], m[1][1], m[1][2]},
            {m[2][0], m[2][1], m[2][2]}
        );
    }

    const float* data() const { return &m[0][0]; }
    float*       data()       { return &m[0][0]; }

    static Mat4 identity() { return Mat4(); }
    static Mat4 zero()     { Mat4 r; std::fill(&r.m[0][0], &r.m[0][0]+16, 0.0f); return r; }

    // Translation
    static Mat4 from_translation(float tx, float ty, float tz) {
        Mat4 r;
        r.m[3][0]=tx; r.m[3][1]=ty; r.m[3][2]=tz;
        return r;
    }
    static Mat4 from_translation(const Vec3& t) { return from_translation(t.x, t.y, t.z); }

    // Scale
    static Mat4 from_scale(float sx, float sy, float sz) {
        Mat4 r = zero();
        r.m[0][0]=sx; r.m[1][1]=sy; r.m[2][2]=sz; r.m[3][3]=1;
        return r;
    }
    static Mat4 from_scale(const Vec3& s) { return from_scale(s.x, s.y, s.z); }
    static Mat4 from_scale(float s)       { return from_scale(s, s, s); }

    // Rotation
    static Mat4 from_rotation_x(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        Mat4 r;
        r.m[1][1]=c;  r.m[2][1]=-s;
        r.m[1][2]=s;  r.m[2][2]=c;
        return r;
    }

    static Mat4 from_rotation_y(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        Mat4 r;
        r.m[0][0]=c;  r.m[2][0]=s;
        r.m[0][2]=-s; r.m[2][2]=c;
        return r;
    }

    static Mat4 from_rotation_z(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        Mat4 r;
        r.m[0][0]=c;  r.m[1][0]=-s;
        r.m[0][1]=s;  r.m[1][1]=c;
        return r;
    }

    static Mat4 from_axis_angle(const Vec3& axis, float rad) {
        return Mat4(Mat3::from_axis_angle(axis, rad));
    }

    // TRS composition
    static Mat4 from_trs(const Vec3& translation, const Quat& rotation, const Vec3& scale);

    // View matrix (right-handed, camera at eye looking at target)
    static Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up = Vec3::up()) {
        Vec3 f = normalize(target - eye);
        Vec3 r = normalize(cross(f, up));
        Vec3 u = cross(r, f);
        Mat4 result;
        result.m[0][0]= r.x; result.m[1][0]= r.y; result.m[2][0]= r.z; result.m[3][0]=-dot(r, eye);
        result.m[0][1]= u.x; result.m[1][1]= u.y; result.m[2][1]= u.z; result.m[3][1]=-dot(u, eye);
        result.m[0][2]=-f.x; result.m[1][2]=-f.y; result.m[2][2]=-f.z; result.m[3][2]= dot(f, eye);
        result.m[0][3]=0;    result.m[1][3]=0;     result.m[2][3]=0;    result.m[3][3]=1;
        return result;
    }

    // Perspective projection (fov in radians, right-handed, depth [0,1] or [-1,1])
    static Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) {
        float tan_half = std::tan(fov_y * 0.5f);
        Mat4 r = zero();
        r.m[0][0] = 1.0f / (aspect * tan_half);
        r.m[1][1] = 1.0f / tan_half;
        r.m[2][2] = -(far_plane + near_plane) / (far_plane - near_plane);
        r.m[2][3] = -1.0f;
        r.m[3][2] = -(2.0f * far_plane * near_plane) / (far_plane - near_plane);
        return r;
    }

    // Orthographic projection
    static Mat4 ortho(float left, float right, float bottom, float top, float near_plane, float far_plane) {
        Mat4 r = zero();
        r.m[0][0] = 2.0f / (right - left);
        r.m[1][1] = 2.0f / (top - bottom);
        r.m[2][2] = -2.0f / (far_plane - near_plane);
        r.m[3][0] = -(right + left) / (right - left);
        r.m[3][1] = -(top + bottom) / (top - bottom);
        r.m[3][2] = -(far_plane + near_plane) / (far_plane - near_plane);
        r.m[3][3] = 1.0f;
        return r;
    }

    Mat4 transposed() const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int ri = 0; ri < 4; ++ri)
                r.m[c][ri] = m[ri][c];
        return r;
    }

    float determinant() const {
        float a0 = m[0][0]*m[1][1] - m[0][1]*m[1][0];
        float a1 = m[0][0]*m[1][2] - m[0][2]*m[1][0];
        float a2 = m[0][0]*m[1][3] - m[0][3]*m[1][0];
        float a3 = m[0][1]*m[1][2] - m[0][2]*m[1][1];
        float a4 = m[0][1]*m[1][3] - m[0][3]*m[1][1];
        float a5 = m[0][2]*m[1][3] - m[0][3]*m[1][2];
        float b0 = m[2][0]*m[3][1] - m[2][1]*m[3][0];
        float b1 = m[2][0]*m[3][2] - m[2][2]*m[3][0];
        float b2 = m[2][0]*m[3][3] - m[2][3]*m[3][0];
        float b3 = m[2][1]*m[3][2] - m[2][2]*m[3][1];
        float b4 = m[2][1]*m[3][3] - m[2][3]*m[3][1];
        float b5 = m[2][2]*m[3][3] - m[2][3]*m[3][2];
        return a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
    }

    Mat4 inverse() const {
        float a0 = m[0][0]*m[1][1] - m[0][1]*m[1][0];
        float a1 = m[0][0]*m[1][2] - m[0][2]*m[1][0];
        float a2 = m[0][0]*m[1][3] - m[0][3]*m[1][0];
        float a3 = m[0][1]*m[1][2] - m[0][2]*m[1][1];
        float a4 = m[0][1]*m[1][3] - m[0][3]*m[1][1];
        float a5 = m[0][2]*m[1][3] - m[0][3]*m[1][2];
        float b0 = m[2][0]*m[3][1] - m[2][1]*m[3][0];
        float b1 = m[2][0]*m[3][2] - m[2][2]*m[3][0];
        float b2 = m[2][0]*m[3][3] - m[2][3]*m[3][0];
        float b3 = m[2][1]*m[3][2] - m[2][2]*m[3][1];
        float b4 = m[2][1]*m[3][3] - m[2][3]*m[3][1];
        float b5 = m[2][2]*m[3][3] - m[2][3]*m[3][2];

        float det = a0*b5 - a1*b4 + a2*b3 + a3*b2 - a4*b1 + a5*b0;
        if (std::abs(det) < EPSILON) return identity();
        float inv = 1.0f / det;

        Mat4 r;
        r.m[0][0] = ( m[1][1]*b5 - m[1][2]*b4 + m[1][3]*b3) * inv;
        r.m[0][1] = (-m[0][1]*b5 + m[0][2]*b4 - m[0][3]*b3) * inv;
        r.m[0][2] = ( m[3][1]*a5 - m[3][2]*a4 + m[3][3]*a3) * inv;
        r.m[0][3] = (-m[2][1]*a5 + m[2][2]*a4 - m[2][3]*a3) * inv;
        r.m[1][0] = (-m[1][0]*b5 + m[1][2]*b2 - m[1][3]*b1) * inv;
        r.m[1][1] = ( m[0][0]*b5 - m[0][2]*b2 + m[0][3]*b1) * inv;
        r.m[1][2] = (-m[3][0]*a5 + m[3][2]*a2 - m[3][3]*a1) * inv;
        r.m[1][3] = ( m[2][0]*a5 - m[2][2]*a2 + m[2][3]*a1) * inv;
        r.m[2][0] = ( m[1][0]*b4 - m[1][1]*b2 + m[1][3]*b0) * inv;
        r.m[2][1] = (-m[0][0]*b4 + m[0][1]*b2 - m[0][3]*b0) * inv;
        r.m[2][2] = ( m[3][0]*a4 - m[3][1]*a2 + m[3][3]*a0) * inv;
        r.m[2][3] = (-m[2][0]*a4 + m[2][1]*a2 - m[2][3]*a0) * inv;
        r.m[3][0] = (-m[1][0]*b3 + m[1][1]*b1 - m[1][2]*b0) * inv;
        r.m[3][1] = ( m[0][0]*b3 - m[0][1]*b1 + m[0][2]*b0) * inv;
        r.m[3][2] = (-m[3][0]*a3 + m[3][1]*a1 - m[3][2]*a0) * inv;
        r.m[3][3] = ( m[2][0]*a3 - m[2][1]*a1 + m[2][2]*a0) * inv;
        return r;
    }

    // Extract translation component
    Vec3 get_translation() const { return {m[3][0], m[3][1], m[3][2]}; }

    void set_translation(const Vec3& t) { m[3][0]=t.x; m[3][1]=t.y; m[3][2]=t.z; }

    // Extract scale (approximate, assumes no shear)
    Vec3 get_scale() const {
        return {
            math::length(Vec3(m[0][0], m[0][1], m[0][2])),
            math::length(Vec3(m[1][0], m[1][1], m[1][2])),
            math::length(Vec3(m[2][0], m[2][1], m[2][2]))
        };
    }

    // Extract rotation as Mat3 (removes scale)
    Mat3 get_rotation_mat3() const {
        Vec3 s = get_scale();
        Mat3 r;
        r.m[0][0]=m[0][0]/s.x; r.m[0][1]=m[0][1]/s.x; r.m[0][2]=m[0][2]/s.x;
        r.m[1][0]=m[1][0]/s.y; r.m[1][1]=m[1][1]/s.y; r.m[1][2]=m[1][2]/s.y;
        r.m[2][0]=m[2][0]/s.z; r.m[2][1]=m[2][1]/s.z; r.m[2][2]=m[2][2]/s.z;
        return r;
    }

    // Transform point (w=1, applies translation)
    Vec3 transform_point(const Vec3& p) const {
        float w = m[0][3]*p.x + m[1][3]*p.y + m[2][3]*p.z + m[3][3];
        float inv_w = (std::abs(w) > EPSILON) ? 1.0f / w : 1.0f;
        return {
            (m[0][0]*p.x + m[1][0]*p.y + m[2][0]*p.z + m[3][0]) * inv_w,
            (m[0][1]*p.x + m[1][1]*p.y + m[2][1]*p.z + m[3][1]) * inv_w,
            (m[0][2]*p.x + m[1][2]*p.y + m[2][2]*p.z + m[3][2]) * inv_w
        };
    }

    // Transform direction (w=0, ignores translation)
    Vec3 transform_direction(const Vec3& d) const {
        return {
            m[0][0]*d.x + m[1][0]*d.y + m[2][0]*d.z,
            m[0][1]*d.x + m[1][1]*d.y + m[2][1]*d.z,
            m[0][2]*d.x + m[1][2]*d.y + m[2][2]*d.z
        };
    }
};

#if defined(WINDOW_MATH_SIMD)
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    simd::mat4_mul(&a.m[0][0], &b.m[0][0], &r.m[0][0]);
    return r;
}

inline Vec4 operator*(const Mat4& m, const Vec4& v) {
    return Vec4(simd::mat4_mul_vec(&m.m[0][0], v.simd));
}

inline Mat4 operator*(const Mat4& m, float s) {
    Mat4 r;
    simd::f128 sv = simd::splat(s);
    for (int c = 0; c < 4; ++c)
        simd::store4a(&r.m[c][0], simd::mul(simd::load4a(&m.m[c][0]), sv));
    return r;
}

inline Mat4 operator+(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        simd::store4a(&r.m[c][0], simd::add(simd::load4a(&a.m[c][0]), simd::load4a(&b.m[c][0])));
    return r;
}

inline Mat4 operator-(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        simd::store4a(&r.m[c][0], simd::sub(simd::load4a(&a.m[c][0]), simd::load4a(&b.m[c][0])));
    return r;
}
#else
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r = Mat4::zero();
    for (int c = 0; c < 4; ++c)
        for (int ri = 0; ri < 4; ++ri)
            for (int k = 0; k < 4; ++k)
                r.m[c][ri] += a.m[k][ri] * b.m[c][k];
    return r;
}

inline Vec4 operator*(const Mat4& m, const Vec4& v) {
    return {
        m.m[0][0]*v.x + m.m[1][0]*v.y + m.m[2][0]*v.z + m.m[3][0]*v.w,
        m.m[0][1]*v.x + m.m[1][1]*v.y + m.m[2][1]*v.z + m.m[3][1]*v.w,
        m.m[0][2]*v.x + m.m[1][2]*v.y + m.m[2][2]*v.z + m.m[3][2]*v.w,
        m.m[0][3]*v.x + m.m[1][3]*v.y + m.m[2][3]*v.z + m.m[3][3]*v.w
    };
}

inline Mat4 operator*(const Mat4& m, float s) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) (&r.m[0][0])[i] = (&m.m[0][0])[i] * s;
    return r;
}

inline Mat4 operator+(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) (&r.m[0][0])[i] = (&a.m[0][0])[i] + (&b.m[0][0])[i];
    return r;
}

inline Mat4 operator-(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) (&r.m[0][0])[i] = (&a.m[0][0])[i] - (&b.m[0][0])[i];
    return r;
}
#endif

inline Mat4& operator*=(Mat4& a, const Mat4& b) { a = a * b; return a; }

// ============================================================================
// Quaternion <-> Matrix deferred implementations
// ============================================================================

inline Quat Quat::from_mat3(const Mat3& m) {
    float trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
    Quat q;
    if (trace > 0.0f) {
        float s = 0.5f / std::sqrt(trace + 1.0f);
        q.w() = 0.25f / s;
        q.x() = (m.m[1][2] - m.m[2][1]) * s;
        q.y() = (m.m[2][0] - m.m[0][2]) * s;
        q.z() = (m.m[0][1] - m.m[1][0]) * s;
    } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        float s = 2.0f * std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]);
        q.w() = (m.m[1][2] - m.m[2][1]) / s;
        q.x() = 0.25f * s;
        q.y() = (m.m[1][0] + m.m[0][1]) / s;
        q.z() = (m.m[2][0] + m.m[0][2]) / s;
    } else if (m.m[1][1] > m.m[2][2]) {
        float s = 2.0f * std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]);
        q.w() = (m.m[2][0] - m.m[0][2]) / s;
        q.x() = (m.m[1][0] + m.m[0][1]) / s;
        q.y() = 0.25f * s;
        q.z() = (m.m[2][1] + m.m[1][2]) / s;
    } else {
        float s = 2.0f * std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]);
        q.w() = (m.m[0][1] - m.m[1][0]) / s;
        q.x() = (m.m[2][0] + m.m[0][2]) / s;
        q.y() = (m.m[2][1] + m.m[1][2]) / s;
        q.z() = 0.25f * s;
    }
    return q.normalized();
}

inline Quat Quat::from_mat4(const Mat4& m) {
    return from_mat3(m.upper_left_3x3());
}

inline Quat Quat::look_rotation(const Vec3& forward, const Vec3& up) {
    Vec3 f = normalize(forward);
    Vec3 r = normalize(cross(up, f));
    Vec3 u = cross(f, r);
    // Note: forward maps to -Z (right-handed convention)
    Mat3 m(r, u, -f);
    return from_mat3(m);
}

inline Mat3 Quat::to_mat3() const {
    float xx = v.x*v.x, yy = v.y*v.y, zz = v.z*v.z;
    float xy = v.x*v.y, xz = v.x*v.z, yz = v.y*v.z;
    float wx = v.w*v.x, wy = v.w*v.y, wz = v.w*v.z;
    return Mat3(
        1-2*(yy+zz),   2*(xy+wz),   2*(xz-wy),
        2*(xy-wz),   1-2*(xx+zz),   2*(yz+wx),
        2*(xz+wy),     2*(yz-wx), 1-2*(xx+yy)
    );
}

inline Mat4 Quat::to_mat4() const {
    return Mat4(to_mat3());
}

inline Mat4 Mat4::from_trs(const Vec3& translation, const Quat& rotation, const Vec3& scale) {
    Mat4 r(rotation.to_mat3());
    r.m[0][0]*=scale.x; r.m[0][1]*=scale.x; r.m[0][2]*=scale.x;
    r.m[1][0]*=scale.y; r.m[1][1]*=scale.y; r.m[1][2]*=scale.y;
    r.m[2][0]*=scale.z; r.m[2][1]*=scale.z; r.m[2][2]*=scale.z;
    r.m[3][0]=translation.x; r.m[3][1]=translation.y; r.m[3][2]=translation.z;
    return r;
}

// ============================================================================
// AABB - 3D Axis-Aligned Bounding Box
// ============================================================================

struct AABB {
    Vec3 min_pt;
    Vec3 max_pt;

    AABB() : min_pt(Vec3(0)), max_pt(Vec3(0)) {}
    AABB(const Vec3& mn, const Vec3& mx) : min_pt(mn), max_pt(mx) {}

    static AABB from_center_extents(const Vec3& center, const Vec3& extents) {
        return {center - extents, center + extents};
    }

    static AABB from_center_size(const Vec3& center, const Vec3& size) {
        Vec3 half = size * 0.5f;
        return {center - half, center + half};
    }

    // Build AABB that encloses a set of points
    static AABB from_points(const Vec3* points, std::size_t count) {
        if (count == 0) return AABB();
        Vec3 mn = points[0], mx = points[0];
        for (std::size_t i = 1; i < count; ++i) {
            mn = min(mn, points[i]);
            mx = max(mx, points[i]);
        }
        return {mn, mx};
    }

    Vec3 center() const  { return (min_pt + max_pt) * 0.5f; }
    Vec3 size() const    { return max_pt - min_pt; }
    Vec3 extents() const { return (max_pt - min_pt) * 0.5f; }

    float volume() const {
        Vec3 s = size();
        return s.x * s.y * s.z;
    }

    float surface_area() const {
        Vec3 s = size();
        return 2.0f * (s.x*s.y + s.y*s.z + s.z*s.x);
    }

    bool contains(const Vec3& p) const {
        return p.x >= min_pt.x && p.x <= max_pt.x
            && p.y >= min_pt.y && p.y <= max_pt.y
            && p.z >= min_pt.z && p.z <= max_pt.z;
    }

    bool contains(const AABB& other) const {
        return other.min_pt.x >= min_pt.x && other.max_pt.x <= max_pt.x
            && other.min_pt.y >= min_pt.y && other.max_pt.y <= max_pt.y
            && other.min_pt.z >= min_pt.z && other.max_pt.z <= max_pt.z;
    }

    bool intersects(const AABB& other) const {
        return min_pt.x <= other.max_pt.x && max_pt.x >= other.min_pt.x
            && min_pt.y <= other.max_pt.y && max_pt.y >= other.min_pt.y
            && min_pt.z <= other.max_pt.z && max_pt.z >= other.min_pt.z;
    }

    // Compute intersection (call only if intersects() is true)
    AABB intersection(const AABB& other) const {
        return {max(min_pt, other.min_pt), min(max_pt, other.max_pt)};
    }

    AABB merged(const AABB& other) const {
        return {min(min_pt, other.min_pt), max(max_pt, other.max_pt)};
    }

    AABB expanded(float amount) const {
        Vec3 e(amount);
        return {min_pt - e, max_pt + e};
    }

    AABB expanded(const Vec3& amount) const {
        return {min_pt - amount, max_pt + amount};
    }

    AABB translated(const Vec3& offset) const {
        return {min_pt + offset, max_pt + offset};
    }

    // Closest point on AABB surface to a point
    Vec3 closest_point(const Vec3& p) const {
        return {
            std::clamp(p.x, min_pt.x, max_pt.x),
            std::clamp(p.y, min_pt.y, max_pt.y),
            std::clamp(p.z, min_pt.z, max_pt.z)
        };
    }

    float distance_to(const Vec3& p) const {
        return math::distance(p, closest_point(p));
    }

    float distance_squared_to(const Vec3& p) const {
        return math::distance_squared(p, closest_point(p));
    }

    // Transform AABB by matrix (returns new AABB enclosing transformed corners)
    AABB transformed(const Mat4& mat) const {
        Vec3 corners[8] = {
            {min_pt.x, min_pt.y, min_pt.z}, {max_pt.x, min_pt.y, min_pt.z},
            {min_pt.x, max_pt.y, min_pt.z}, {max_pt.x, max_pt.y, min_pt.z},
            {min_pt.x, min_pt.y, max_pt.z}, {max_pt.x, min_pt.y, max_pt.z},
            {min_pt.x, max_pt.y, max_pt.z}, {max_pt.x, max_pt.y, max_pt.z}
        };
        Vec3 mn = mat.transform_point(corners[0]);
        Vec3 mx = mn;
        for (int i = 1; i < 8; ++i) {
            Vec3 t = mat.transform_point(corners[i]);
            mn = min(mn, t);
            mx = max(mx, t);
        }
        return {mn, mx};
    }

    // Ray-AABB intersection (returns false if no hit)
    bool ray_intersect(const Vec3& origin, const Vec3& dir,
                       float* out_t_near = nullptr, float* out_t_far = nullptr) const {
        Vec3 inv_dir(1.0f/dir.x, 1.0f/dir.y, 1.0f/dir.z);
        Vec3 t1 = (min_pt - origin) * inv_dir;
        Vec3 t2 = (max_pt - origin) * inv_dir;
        Vec3 tmin_v = min(t1, t2);
        Vec3 tmax_v = max(t1, t2);
        float t_near = std::max({tmin_v.x, tmin_v.y, tmin_v.z});
        float t_far  = std::min({tmax_v.x, tmax_v.y, tmax_v.z});
        if (out_t_near) *out_t_near = t_near;
        if (out_t_far)  *out_t_far  = t_far;
        return t_near <= t_far && t_far >= 0.0f;
    }

    // Get one of the 8 corners (index 0..7)
    Vec3 corner(int index) const {
        return {
            (index & 1) ? max_pt.x : min_pt.x,
            (index & 2) ? max_pt.y : min_pt.y,
            (index & 4) ? max_pt.z : min_pt.z
        };
    }
};

inline bool operator==(const AABB& a, const AABB& b) { return a.min_pt==b.min_pt && a.max_pt==b.max_pt; }
inline bool operator!=(const AABB& a, const AABB& b) { return !(a == b); }

// ============================================================================
// Sphere
// ============================================================================

struct Sphere {
    Vec3 center;
    float radius;

    Sphere() : center(Vec3::zero()), radius(0) {}
    Sphere(const Vec3& c, float r) : center(c), radius(r) {}

    // Build bounding sphere from points (simple, not optimal)
    static Sphere from_points(const Vec3* points, std::size_t count) {
        if (count == 0) return Sphere();
        AABB box = AABB::from_points(points, count);
        Vec3 c = box.center();
        float r = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            r = std::max(r, math::distance(c, points[i]));
        }
        return {c, r};
    }

    static Sphere from_aabb(const AABB& box) {
        Vec3 c = box.center();
        return {c, math::length(box.extents())};
    }

    bool contains(const Vec3& p) const {
        return math::distance_squared(center, p) <= radius * radius;
    }

    bool contains(const Sphere& other) const {
        return math::distance(center, other.center) + other.radius <= radius;
    }

    bool intersects(const Sphere& other) const {
        float r_sum = radius + other.radius;
        return math::distance_squared(center, other.center) <= r_sum * r_sum;
    }

    bool intersects(const AABB& box) const {
        return box.distance_squared_to(center) <= radius * radius;
    }

    Sphere merged(const Sphere& other) const {
        Vec3 d = other.center - center;
        float dist = math::length(d);
        if (dist + other.radius <= radius) return *this;
        if (dist + radius <= other.radius) return other;
        float new_radius = (dist + radius + other.radius) * 0.5f;
        Vec3 new_center = center + d * ((new_radius - radius) / dist);
        return {new_center, new_radius};
    }

    Sphere translated(const Vec3& offset) const {
        return {center + offset, radius};
    }

    Sphere scaled(float s) const {
        return {center, radius * s};
    }

    // Closest point on sphere surface to a point
    Vec3 closest_point(const Vec3& p) const {
        Vec3 d = p - center;
        float len = math::length(d);
        if (len < EPSILON) return center + Vec3(radius, 0, 0);
        return center + d * (radius / len);
    }

    float distance_to(const Vec3& p) const {
        return std::max(0.0f, math::distance(center, p) - radius);
    }

    // Ray-sphere intersection
    bool ray_intersect(const Vec3& origin, const Vec3& dir,
                       float* out_t1 = nullptr, float* out_t2 = nullptr) const {
        Vec3 oc = origin - center;
        float b = dot(oc, dir);
        float c = dot(oc, oc) - radius * radius;
        float discriminant = b*b - c;
        if (discriminant < 0.0f) return false;
        float sq = std::sqrt(discriminant);
        if (out_t1) *out_t1 = -b - sq;
        if (out_t2) *out_t2 = -b + sq;
        return true;
    }

    AABB to_aabb() const {
        Vec3 e(radius);
        return {center - e, center + e};
    }
};

inline bool operator==(const Sphere& a, const Sphere& b) {
    return a.center == b.center && a.radius == b.radius;
}
inline bool operator!=(const Sphere& a, const Sphere& b) { return !(a == b); }

// ============================================================================
// OBB - 3D Oriented Bounding Box
// ============================================================================

struct OBB {
    Vec3 center;
    Vec3 half_extents;  // half-size along each local axis
    Quat orientation;   // rotation from local to world

    OBB() : center(Vec3::zero()), half_extents(Vec3::zero()), orientation(Quat::identity()) {}
    OBB(const Vec3& c, const Vec3& he, const Quat& ori = Quat::identity())
        : center(c), half_extents(he), orientation(ori) {}

    static OBB from_aabb(const AABB& box) {
        return OBB(box.center(), box.extents());
    }

    // Get the 3 local axes in world space
    Vec3 axis_x() const { return orientation.rotate(Vec3::unit_x()); }
    Vec3 axis_y() const { return orientation.rotate(Vec3::unit_y()); }
    Vec3 axis_z() const { return orientation.rotate(Vec3::unit_z()); }

    void get_axes(Vec3* axes) const {
        axes[0] = axis_x();
        axes[1] = axis_y();
        axes[2] = axis_z();
    }

    // Get all 8 corners
    void get_corners(Vec3* corners) const {
        Vec3 ax = axis_x() * half_extents.x;
        Vec3 ay = axis_y() * half_extents.y;
        Vec3 az = axis_z() * half_extents.z;
        corners[0] = center - ax - ay - az;
        corners[1] = center + ax - ay - az;
        corners[2] = center - ax + ay - az;
        corners[3] = center + ax + ay - az;
        corners[4] = center - ax - ay + az;
        corners[5] = center + ax - ay + az;
        corners[6] = center - ax + ay + az;
        corners[7] = center + ax + ay + az;
    }

    bool contains(const Vec3& p) const {
        Vec3 local = orientation.conjugate().rotate(p - center);
        return std::abs(local.x) <= half_extents.x
            && std::abs(local.y) <= half_extents.y
            && std::abs(local.z) <= half_extents.z;
    }

    Vec3 closest_point(const Vec3& p) const {
        Vec3 d = p - center;
        Vec3 axes[3];
        get_axes(axes);
        Vec3 result = center;
        for (int i = 0; i < 3; ++i) {
            float dist = dot(d, axes[i]);
            dist = std::clamp(dist, -half_extents[i], half_extents[i]);
            result = result + axes[i] * dist;
        }
        return result;
    }

    float distance_to(const Vec3& p) const {
        return math::distance(p, closest_point(p));
    }

    // SAT-based OBB vs OBB intersection test
    bool intersects(const OBB& other) const {
        Vec3 a_axes[3], b_axes[3];
        get_axes(a_axes);
        other.get_axes(b_axes);

        Mat3 R, absR;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                R.m[j][i] = dot(a_axes[i], b_axes[j]);
                absR.m[j][i] = std::abs(R.m[j][i]) + EPSILON;
            }

        Vec3 t_vec = other.center - center;
        float t[3] = {dot(t_vec, a_axes[0]), dot(t_vec, a_axes[1]), dot(t_vec, a_axes[2])};

        // Test axes A0, A1, A2
        for (int i = 0; i < 3; ++i) {
            float ra = half_extents[i];
            float rb = other.half_extents.x*absR.m[0][i] + other.half_extents.y*absR.m[1][i] + other.half_extents.z*absR.m[2][i];
            if (std::abs(t[i]) > ra + rb) return false;
        }

        // Test axes B0, B1, B2
        for (int i = 0; i < 3; ++i) {
            float ra = half_extents.x*absR.m[i][0] + half_extents.y*absR.m[i][1] + half_extents.z*absR.m[i][2];
            float rb = other.half_extents[i];
            float proj = std::abs(t[0]*R.m[i][0] + t[1]*R.m[i][1] + t[2]*R.m[i][2]);
            if (proj > ra + rb) return false;
        }

        // Test cross product axes (A0xB0, A0xB1, ..., A2xB2)
        float ra, rb, proj;

        ra = half_extents.y*absR.m[0][2] + half_extents.z*absR.m[0][1];
        rb = other.half_extents.y*absR.m[2][0] + other.half_extents.z*absR.m[1][0];
        proj = std::abs(t[2]*R.m[0][1] - t[1]*R.m[0][2]);
        if (proj > ra + rb) return false;

        ra = half_extents.y*absR.m[1][2] + half_extents.z*absR.m[1][1];
        rb = other.half_extents.x*absR.m[2][0] + other.half_extents.z*absR.m[0][0];
        proj = std::abs(t[2]*R.m[1][1] - t[1]*R.m[1][2]);
        if (proj > ra + rb) return false;

        ra = half_extents.y*absR.m[2][2] + half_extents.z*absR.m[2][1];
        rb = other.half_extents.x*absR.m[1][0] + other.half_extents.y*absR.m[0][0];
        proj = std::abs(t[2]*R.m[2][1] - t[1]*R.m[2][2]);
        if (proj > ra + rb) return false;

        ra = half_extents.x*absR.m[0][2] + half_extents.z*absR.m[0][0];
        rb = other.half_extents.y*absR.m[2][1] + other.half_extents.z*absR.m[1][1];
        proj = std::abs(t[0]*R.m[0][2] - t[2]*R.m[0][0]);
        if (proj > ra + rb) return false;

        ra = half_extents.x*absR.m[1][2] + half_extents.z*absR.m[1][0];
        rb = other.half_extents.x*absR.m[2][1] + other.half_extents.z*absR.m[0][1];
        proj = std::abs(t[0]*R.m[1][2] - t[2]*R.m[1][0]);
        if (proj > ra + rb) return false;

        ra = half_extents.x*absR.m[2][2] + half_extents.z*absR.m[2][0];
        rb = other.half_extents.x*absR.m[1][1] + other.half_extents.y*absR.m[0][1];
        proj = std::abs(t[0]*R.m[2][2] - t[2]*R.m[2][0]);
        if (proj > ra + rb) return false;

        ra = half_extents.x*absR.m[0][1] + half_extents.y*absR.m[0][0];
        rb = other.half_extents.y*absR.m[2][2] + other.half_extents.z*absR.m[1][2];
        proj = std::abs(t[1]*R.m[0][0] - t[0]*R.m[0][1]);
        if (proj > ra + rb) return false;

        ra = half_extents.x*absR.m[1][1] + half_extents.y*absR.m[1][0];
        rb = other.half_extents.x*absR.m[2][2] + other.half_extents.z*absR.m[0][2];
        proj = std::abs(t[1]*R.m[1][0] - t[0]*R.m[1][1]);
        if (proj > ra + rb) return false;

        ra = half_extents.x*absR.m[2][1] + half_extents.y*absR.m[2][0];
        rb = other.half_extents.x*absR.m[1][2] + other.half_extents.y*absR.m[0][2];
        proj = std::abs(t[1]*R.m[2][0] - t[0]*R.m[2][1]);
        if (proj > ra + rb) return false;

        return true;
    }

    bool intersects(const AABB& aabb) const {
        return intersects(OBB::from_aabb(aabb));
    }

    bool intersects(const Sphere& sphere) const {
        return distance_to(sphere.center) <= sphere.radius;
    }

    AABB to_aabb() const {
        Vec3 corners[8];
        get_corners(corners);
        return AABB::from_points(corners, 8);
    }

    OBB translated(const Vec3& offset) const {
        return OBB(center + offset, half_extents, orientation);
    }
};

// ============================================================================
// Frustum - View frustum for culling (6 planes)
// ============================================================================

struct Plane {
    Vec3  normal;
    float distance; // signed distance from origin

    Plane() : normal(Vec3::unit_y()), distance(0) {}
    Plane(const Vec3& n, float d) : normal(n), distance(d) {}
    Plane(const Vec3& n, const Vec3& point) : normal(n), distance(-dot(n, point)) {}

    // Build from three points (counter-clockwise winding)
    static Plane from_points(const Vec3& a, const Vec3& b, const Vec3& c) {
        Vec3 n = normalize(cross(b - a, c - a));
        return {n, -dot(n, a)};
    }

    Plane normalized() const {
        float len = math::length(normal);
        if (len < EPSILON) return *this;
        return {normal / len, distance / len};
    }

    float distance_to(const Vec3& p) const {
        return dot(normal, p) + distance;
    }
};

struct Frustum {
    Plane planes[6]; // left, right, bottom, top, near, far

    Frustum() = default;

    // Extract from view-projection matrix
    static Frustum from_matrix(const Mat4& vp) {
        Frustum f;
        // Left
        f.planes[0] = Plane(
            {vp(3,0)+vp(0,0), vp(3,1)+vp(0,1), vp(3,2)+vp(0,2)},
            vp(3,3)+vp(0,3)
        ).normalized();
        // Right
        f.planes[1] = Plane(
            {vp(3,0)-vp(0,0), vp(3,1)-vp(0,1), vp(3,2)-vp(0,2)},
            vp(3,3)-vp(0,3)
        ).normalized();
        // Bottom
        f.planes[2] = Plane(
            {vp(3,0)+vp(1,0), vp(3,1)+vp(1,1), vp(3,2)+vp(1,2)},
            vp(3,3)+vp(1,3)
        ).normalized();
        // Top
        f.planes[3] = Plane(
            {vp(3,0)-vp(1,0), vp(3,1)-vp(1,1), vp(3,2)-vp(1,2)},
            vp(3,3)-vp(1,3)
        ).normalized();
        // Near
        f.planes[4] = Plane(
            {vp(3,0)+vp(2,0), vp(3,1)+vp(2,1), vp(3,2)+vp(2,2)},
            vp(3,3)+vp(2,3)
        ).normalized();
        // Far
        f.planes[5] = Plane(
            {vp(3,0)-vp(2,0), vp(3,1)-vp(2,1), vp(3,2)-vp(2,2)},
            vp(3,3)-vp(2,3)
        ).normalized();
        return f;
    }

    bool contains(const Vec3& point) const {
        for (int i = 0; i < 6; ++i)
            if (planes[i].distance_to(point) < 0.0f) return false;
        return true;
    }

    bool intersects(const Sphere& sphere) const {
        for (int i = 0; i < 6; ++i)
            if (planes[i].distance_to(sphere.center) < -sphere.radius) return false;
        return true;
    }

    bool intersects(const AABB& box) const {
        for (int i = 0; i < 6; ++i) {
            Vec3 p;
            p.x = (planes[i].normal.x >= 0) ? box.max_pt.x : box.min_pt.x;
            p.y = (planes[i].normal.y >= 0) ? box.max_pt.y : box.min_pt.y;
            p.z = (planes[i].normal.z >= 0) ? box.max_pt.z : box.min_pt.z;
            if (planes[i].distance_to(p) < 0.0f) return false;
        }
        return true;
    }
};

// ============================================================================
// Batch Operations (SIMD-accelerated)
// ============================================================================

// Transform array of Vec3 points by Mat4
inline void batch_transform_points(const Mat4& mat, const Vec3* in, Vec3* out, std::size_t count) {
#if defined(WINDOW_MATH_SIMD)
    for (std::size_t i = 0; i < count; ++i) {
        simd::f128 v = simd::set(in[i].x, in[i].y, in[i].z, 1.0f);
        simd::f128 r = simd::mat4_mul_vec(&mat.m[0][0], v);
        alignas(16) float tmp[4];
        simd::store4a(tmp, r);
        // Perspective divide
        float inv_w = (std::abs(tmp[3]) > EPSILON) ? 1.0f / tmp[3] : 1.0f;
        out[i] = {tmp[0] * inv_w, tmp[1] * inv_w, tmp[2] * inv_w};
    }
#else
    for (std::size_t i = 0; i < count; ++i)
        out[i] = mat.transform_point(in[i]);
#endif
}

// Transform array of Vec3 directions by Mat4 (no translation)
inline void batch_transform_directions(const Mat4& mat, const Vec3* in, Vec3* out, std::size_t count) {
#if defined(WINDOW_MATH_SIMD)
    for (std::size_t i = 0; i < count; ++i) {
        simd::f128 v = simd::set(in[i].x, in[i].y, in[i].z, 0.0f);
        simd::f128 r = simd::mat4_mul_vec(&mat.m[0][0], v);
        alignas(16) float tmp[4];
        simd::store4a(tmp, r);
        out[i] = {tmp[0], tmp[1], tmp[2]};
    }
#else
    for (std::size_t i = 0; i < count; ++i)
        out[i] = mat.transform_direction(in[i]);
#endif
}

// Transform array of Vec4 by Mat4
inline void batch_transform_vec4(const Mat4& mat, const Vec4* in, Vec4* out, std::size_t count) {
#if defined(WINDOW_MATH_SIMD)
    for (std::size_t i = 0; i < count; ++i)
        out[i] = Vec4(simd::mat4_mul_vec(&mat.m[0][0], in[i].simd));
#else
    for (std::size_t i = 0; i < count; ++i)
        out[i] = mat * in[i];
#endif
}

// Batch dot product of Vec3 arrays
inline void batch_dot3(const Vec3* a, const Vec3* b, float* out, std::size_t count) {
#if defined(WINDOW_MATH_SIMD)
    for (std::size_t i = 0; i < count; ++i)
        out[i] = simd::dot3(simd::load3(&a[i].x), simd::load3(&b[i].x));
#else
    for (std::size_t i = 0; i < count; ++i)
        out[i] = dot(a[i], b[i]);
#endif
}

// Batch cross product of Vec3 arrays
inline void batch_cross3(const Vec3* a, const Vec3* b, Vec3* out, std::size_t count) {
#if defined(WINDOW_MATH_SIMD)
    for (std::size_t i = 0; i < count; ++i) {
        simd::f128 r = simd::cross3(simd::load3(&a[i].x), simd::load3(&b[i].x));
        alignas(16) float tmp[4];
        simd::store4a(tmp, r);
        out[i] = {tmp[0], tmp[1], tmp[2]};
    }
#else
    for (std::size_t i = 0; i < count; ++i)
        out[i] = cross(a[i], b[i]);
#endif
}

// Batch normalize Vec3 array
inline void batch_normalize3(const Vec3* in, Vec3* out, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        out[i] = normalize(in[i]);
}

// Batch Mat4 multiply (chain of matrices)
inline void batch_mat4_multiply(const Mat4* matrices, std::size_t count, Mat4* out) {
    if (count == 0) return;
    *out = matrices[0];
    for (std::size_t i = 1; i < count; ++i)
        *out = *out * matrices[i];
}

#if defined(WINDOW_MATH_SVE)
// ============================================================================
// SVE Batch Operations (scalable vector, ARM)
// ============================================================================

// SVE-accelerated batch Vec3 dot product
inline void batch_dot3_sve(const Vec3* a, const Vec3* b, float* out, std::size_t count) {
    std::size_t i = 0;
    svbool_t pg = svwhilelt_b32_u64(i, count);
    while (svptest_any(svptrue_b32(), pg)) {
        // Process scalable-width elements at a time
        // Load AoS Vec3 data, compute dot products
        // SVE works best with SoA layout; for AoS Vec3 we fall back to per-element
        std::size_t vl = svcntw();
        std::size_t end = std::min(i + vl, count);
        for (std::size_t j = i; j < end; ++j) {
            out[j] = a[j].x*b[j].x + a[j].y*b[j].y + a[j].z*b[j].z;
        }
        i = end;
        pg = svwhilelt_b32_u64(i, count);
    }
}

// SVE-accelerated batch float multiply-add: out[i] = a[i] * b[i] + c[i]
inline void batch_fma_sve(const float* a, const float* b, const float* c, float* out, std::size_t count) {
    std::size_t i = 0;
    svbool_t pg = svwhilelt_b32_u64(i, count);
    while (svptest_any(svptrue_b32(), pg)) {
        svfloat32_t va = svld1_f32(pg, &a[i]);
        svfloat32_t vb = svld1_f32(pg, &b[i]);
        svfloat32_t vc = svld1_f32(pg, &c[i]);
        svfloat32_t result = svmla_f32_x(pg, vc, va, vb);
        svst1_f32(pg, &out[i], result);
        i += svcntw();
        pg = svwhilelt_b32_u64(i, count);
    }
}

// SVE-accelerated batch float array operations
inline void batch_mul_sve(const float* a, const float* b, float* out, std::size_t count) {
    std::size_t i = 0;
    svbool_t pg = svwhilelt_b32_u64(i, count);
    while (svptest_any(svptrue_b32(), pg)) {
        svfloat32_t va = svld1_f32(pg, &a[i]);
        svfloat32_t vb = svld1_f32(pg, &b[i]);
        svst1_f32(pg, &out[i], svmul_f32_x(pg, va, vb));
        i += svcntw();
        pg = svwhilelt_b32_u64(i, count);
    }
}

inline void batch_add_sve(const float* a, const float* b, float* out, std::size_t count) {
    std::size_t i = 0;
    svbool_t pg = svwhilelt_b32_u64(i, count);
    while (svptest_any(svptrue_b32(), pg)) {
        svfloat32_t va = svld1_f32(pg, &a[i]);
        svfloat32_t vb = svld1_f32(pg, &b[i]);
        svst1_f32(pg, &out[i], svadd_f32_x(pg, va, vb));
        i += svcntw();
        pg = svwhilelt_b32_u64(i, count);
    }
}
#endif // WINDOW_MATH_SVE

} // namespace math
} // namespace window

#endif // WINDOW_MATH_UTIL_HPP
