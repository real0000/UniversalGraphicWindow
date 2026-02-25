/*
 * opengl_caps.inl - Shared OpenGL capability query helper
 *
 * Included by every OpenGL backend (WGL, GLX, EGL, Cocoa, WASM).
 * Assumes an active GL context.  Only uses tokens available in GL 3.2 / ES 3.0;
 * optional GL 4.x / ES 3.1 queries are guarded.
 *
 * Call: fill_gl_capabilities(c)  where c is a GraphicsCapabilities&.
 */

#ifndef WINDOW_OPENGL_CAPS_INL
#define WINDOW_OPENGL_CAPS_INL

#include "../graphics_api.hpp"
#include <cstring>  // strstr

static void fill_gl_capabilities(window::GraphicsCapabilities& c) {
    GLint v = 0;

    //-------------------------------------------------------------------------
    // API version
    //-------------------------------------------------------------------------
    glGetIntegerv(GL_MAJOR_VERSION, &v); c.api_version_major = v;
    glGetIntegerv(GL_MINOR_VERSION, &v); c.api_version_minor = v;
    const int gl_ver = c.api_version_major * 10 + c.api_version_minor;  // e.g. 45 = 4.5

    // Detect WebGL / GLES: check GL_VERSION string prefix
    bool is_gles = false;
    const char* ver_str = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (ver_str && (strstr(ver_str, "OpenGL ES") || strstr(ver_str, "WebGL")))
        is_gles = true;

    c.shader_model = static_cast<float>(c.api_version_major) + c.api_version_minor * 0.1f;

    //-------------------------------------------------------------------------
    // Texture limits
    //-------------------------------------------------------------------------
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);               c.max_texture_size = v;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &v);            c.max_texture_3d_size = v;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &v);      c.max_texture_cube_size = v;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &v);       c.max_texture_array_layers = v;

    c.max_mip_levels = 1;
    { int s = c.max_texture_size; while (s > 1) { s >>= 1; ++c.max_mip_levels; } }

    //-------------------------------------------------------------------------
    // Framebuffer limits
    //-------------------------------------------------------------------------
    glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &v);          c.max_color_attachments = v;
    glGetIntegerv(GL_MAX_SAMPLES, &v);                    c.max_samples = v;

    // GL_MAX_FRAMEBUFFER_WIDTH/HEIGHT are GL 4.3 / ES 3.1 — guard with version
#if defined(GL_MAX_FRAMEBUFFER_WIDTH)
    if (!is_gles || gl_ver >= 31) {
        glGetIntegerv(GL_MAX_FRAMEBUFFER_WIDTH,  &v); c.max_framebuffer_width  = v;
        glGetIntegerv(GL_MAX_FRAMEBUFFER_HEIGHT, &v); c.max_framebuffer_height = v;
    }
#endif

    //-------------------------------------------------------------------------
    // Sampling
    //-------------------------------------------------------------------------
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &v); c.max_texture_bindings = v;

    {
        GLfloat af = 1.0f;
#if defined(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &af);
#elif defined(GL_MAX_TEXTURE_MAX_ANISOTROPY)
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &af);
#endif
        c.max_anisotropy = (af > 1.0f) ? static_cast<int>(af) : 1;
    }

    //-------------------------------------------------------------------------
    // Vertex / buffer limits
    //-------------------------------------------------------------------------
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);          c.max_vertex_attributes = v;
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &v); c.max_uniform_bindings = v;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &v);      c.max_uniform_buffer_size = v;

#if defined(GL_MAX_VERTEX_ATTRIB_BINDINGS)
    if (!is_gles || gl_ver >= 31) {
        glGetIntegerv(GL_MAX_VERTEX_ATTRIB_BINDINGS, &v); c.max_vertex_buffers = v;
    }
#endif
#if defined(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS)
    if (!is_gles || gl_ver >= 31) {
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &v); c.max_storage_bindings = v;
    }
#endif

    //-------------------------------------------------------------------------
    // Viewports / scissor
    //-------------------------------------------------------------------------
#if defined(GL_MAX_VIEWPORTS)
    if (!is_gles) {
        glGetIntegerv(GL_MAX_VIEWPORTS, &v); c.max_viewports = v;
    }
#endif
    c.max_scissor_rects = c.max_viewports;

    //-------------------------------------------------------------------------
    // Compute (GL 4.3+ / ES 3.1+)
    //-------------------------------------------------------------------------
    const bool has_compute = !is_gles ? (gl_ver >= 43) : (gl_ver >= 31);
#if defined(GL_MAX_COMPUTE_WORK_GROUP_SIZE)
    if (has_compute) {
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &v); c.max_compute_group_size_x = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &v); c.max_compute_group_size_y = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &v); c.max_compute_group_size_z = v;
        glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &v); c.max_compute_group_total = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &v); c.max_compute_dispatch_x = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &v); c.max_compute_dispatch_y = v;
        glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &v); c.max_compute_dispatch_z = v;
    }
#endif

    //-------------------------------------------------------------------------
    // Shader / pipeline features
    //-------------------------------------------------------------------------
    if (!is_gles) {
        c.compute_shaders     = gl_ver >= 43;
        c.geometry_shaders    = gl_ver >= 32;
        c.tessellation        = gl_ver >= 40;
        c.instancing          = gl_ver >= 31;
        c.indirect_draw       = gl_ver >= 40;
        c.multi_draw_indirect = gl_ver >= 43;
        c.base_vertex_draw    = gl_ver >= 32;
        c.occlusion_query     = true;
        c.timestamp_query     = gl_ver >= 33;
        c.depth_clamp         = gl_ver >= 32;
        c.fill_mode_wireframe = true;
        c.line_smooth         = true;
        c.independent_blend   = gl_ver >= 40;
        c.dual_source_blend   = gl_ver >= 33;
        c.logic_ops           = true;
        c.cube_map_arrays     = gl_ver >= 40;
        c.read_write_textures = has_compute;
    } else {
        // GLES / WebGL subset
        c.compute_shaders     = has_compute;           // ES 3.1+
        c.geometry_shaders    = false;                 // not in core ES
        c.tessellation        = false;
        c.instancing          = gl_ver >= 30;
        c.indirect_draw       = gl_ver >= 31;
        c.multi_draw_indirect = false;
        c.base_vertex_draw    = gl_ver >= 32;
        c.occlusion_query     = gl_ver >= 30;
        c.timestamp_query     = false;
        c.depth_clamp         = false;
        c.fill_mode_wireframe = false;
        c.line_smooth         = false;
        c.independent_blend   = false;
        c.dual_source_blend   = false;
        c.read_write_textures = has_compute;
    }

    //-------------------------------------------------------------------------
    // Texture features (common)
    //-------------------------------------------------------------------------
    c.texture_arrays         = gl_ver >= 30;
    c.texture_3d             = true;
    c.cube_maps              = true;
    c.render_to_texture      = true;
    c.floating_point_textures = !is_gles || gl_ver >= 30;
    c.integer_textures       = gl_ver >= 30;
    c.srgb_framebuffer       = !is_gles || gl_ver >= 30;
    c.srgb_textures          = !is_gles || gl_ver >= 30;
    c.depth32f               = true;
    c.stencil8               = true;

    //-------------------------------------------------------------------------
    // Compressed texture formats: walk the extension string
    //-------------------------------------------------------------------------
    GLint ext_count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &ext_count);
    for (GLint i = 0; i < ext_count; i++) {
        const char* ext = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (!ext) continue;
        if (strstr(ext, "texture_compression_s3tc"))    c.texture_compression_bc   = true;
        if (strstr(ext, "texture_compression_bptc"))    c.texture_compression_bc   = true;
        if (strstr(ext, "texture_compression_rgtc"))    c.texture_compression_bc   = true;
        if (strstr(ext, "texture_compression_etc2"))    c.texture_compression_etc2 = true;
        if (strstr(ext, "texture_compression_astc_ldr"))c.texture_compression_astc = true;
        if (strstr(ext, "texture_compression_astc_hdr"))c.texture_compression_astc = true;
    }
    // ETC2/EAC is mandated by GL 4.3 core and GLES 3.0 core
    if ((!is_gles && gl_ver >= 43) || (is_gles && gl_ver >= 30))
        c.texture_compression_etc2 = true;
}

#endif // WINDOW_OPENGL_CAPS_INL
