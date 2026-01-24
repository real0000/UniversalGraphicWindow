/*
 * api_opengl_cocoa.mm - OpenGL graphics implementation (macOS/iOS)
 */

#include "window.hpp"

#if defined(__APPLE__) && !defined(WINDOW_NO_OPENGL)

#import <Foundation/Foundation.h>

#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl3.h>
#import <OpenGL/gl3ext.h>
#else
#import <UIKit/UIKit.h>
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>
#endif

#include <string>

namespace window {

//=============================================================================
// macOS OpenGL Implementation
//=============================================================================

#if TARGET_OS_OSX

class GraphicsOpenGLMacOS : public Graphics {
public:
    NSOpenGLContext* context = nil;
    NSOpenGLPixelFormat* pixel_format = nil;
    NSView* view = nil;
    std::string device_name;

    ~GraphicsOpenGLMacOS() override {
        if (context) {
            [NSOpenGLContext clearCurrentContext];
            context = nil;
        }
        pixel_format = nil;
    }

    Backend get_backend() const override { return Backend::OpenGL; }
    const char* get_backend_name() const override { return "OpenGL"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    bool resize(int width, int height) override {
        (void)width;
        (void)height;
        if (context) {
            [context update];
        }
        return true;
    }

    void present() override {
        if (context) {
            [context flushBuffer];
        }
    }

    void make_current() override {
        if (context) {
            [context makeCurrentContext];
        }
    }

    void* native_device() const override { return nullptr; }
    void* native_context() const override { return (__bridge void*)context; }
    void* native_swapchain() const override { return (__bridge void*)pixel_format; }
};

Graphics* create_opengl_graphics_nsview(void* ns_view, int width, int height, const Config& config) {
    @autoreleasepool {
        NSView* view = (__bridge NSView*)ns_view;
        if (!view) return nullptr;

        NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
            NSOpenGLPFAColorSize, static_cast<NSOpenGLPixelFormatAttribute>(config.red_bits + config.green_bits + config.blue_bits),
            NSOpenGLPFAAlphaSize, static_cast<NSOpenGLPixelFormatAttribute>(config.alpha_bits),
            NSOpenGLPFADepthSize, static_cast<NSOpenGLPixelFormatAttribute>(config.depth_bits),
            NSOpenGLPFAStencilSize, static_cast<NSOpenGLPixelFormatAttribute>(config.stencil_bits),
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAAccelerated,
            NSOpenGLPFANoRecovery,
            config.samples > 1 ? NSOpenGLPFASampleBuffers : 0, config.samples > 1 ? 1u : 0u,
            config.samples > 1 ? NSOpenGLPFASamples : 0, static_cast<NSOpenGLPixelFormatAttribute>(config.samples),
            0
        };

        NSOpenGLPixelFormat* pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!pixel_format) {
            // Try fallback without multisampling
            NSOpenGLPixelFormatAttribute fallback_attrs[] = {
                NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
                NSOpenGLPFAColorSize, static_cast<NSOpenGLPixelFormatAttribute>(config.red_bits + config.green_bits + config.blue_bits),
                NSOpenGLPFAAlphaSize, static_cast<NSOpenGLPixelFormatAttribute>(config.alpha_bits),
                NSOpenGLPFADepthSize, static_cast<NSOpenGLPixelFormatAttribute>(config.depth_bits),
                NSOpenGLPFAStencilSize, static_cast<NSOpenGLPixelFormatAttribute>(config.stencil_bits),
                NSOpenGLPFADoubleBuffer,
                NSOpenGLPFAAccelerated,
                0
            };
            pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:fallback_attrs];
            if (!pixel_format) {
                return nullptr;
            }
        }

        // Get shared context if provided
        NSOpenGLContext* shared_context = nil;
        if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::OpenGL) {
            shared_context = (__bridge NSOpenGLContext*)config.shared_graphics->native_context();
        }

        NSOpenGLContext* gl_context = [[NSOpenGLContext alloc] initWithFormat:pixel_format shareContext:shared_context];
        if (!gl_context) {
            return nullptr;
        }

        [gl_context setView:view];
        [gl_context makeCurrentContext];

        GLint swap_interval = config.vsync ? 1 : 0;
        [gl_context setValues:&swap_interval forParameter:NSOpenGLCPSwapInterval];

        GraphicsOpenGLMacOS* gfx = new GraphicsOpenGLMacOS();
        gfx->context = gl_context;
        gfx->pixel_format = pixel_format;
        gfx->device_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

        return gfx;
    }
}

#endif // TARGET_OS_OSX

//=============================================================================
// iOS OpenGL ES Implementation
//=============================================================================

#if TARGET_OS_IOS || TARGET_OS_TV

class GraphicsOpenGLES : public Graphics {
public:
    EAGLContext* context = nil;
    GLuint framebuffer = 0;
    GLuint color_renderbuffer = 0;
    GLuint depth_renderbuffer = 0;
    std::string device_name;

    ~GraphicsOpenGLES() override {
        if ([EAGLContext currentContext] == context) {
            [EAGLContext setCurrentContext:nil];
        }

        if (framebuffer) {
            glDeleteFramebuffers(1, &framebuffer);
        }
        if (color_renderbuffer) {
            glDeleteRenderbuffers(1, &color_renderbuffer);
        }
        if (depth_renderbuffer) {
            glDeleteRenderbuffers(1, &depth_renderbuffer);
        }

        context = nil;
    }

    Backend get_backend() const override { return Backend::OpenGL; }
    const char* get_backend_name() const override { return "OpenGL ES"; }
    const char* get_device_name() const override { return device_name.c_str(); }

    bool resize(int width, int height) override {
        // For iOS, we'd need to recreate renderbuffers on resize
        // This is typically handled by the view's layoutSubviews
        (void)width;
        (void)height;
        return true;
    }

    void present() override {
        if (context && color_renderbuffer) {
            glBindRenderbuffer(GL_RENDERBUFFER, color_renderbuffer);
            [context presentRenderbuffer:GL_RENDERBUFFER];
        }
    }

    void make_current() override {
        if (context) {
            [EAGLContext setCurrentContext:context];
            if (framebuffer) {
                glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            }
        }
    }

    void* native_device() const override { return nullptr; }
    void* native_context() const override { return (__bridge void*)context; }
    void* native_swapchain() const override { return nullptr; }
};

Graphics* create_opengl_graphics_uiview(void* ui_view, int width, int height, const Config& config) {
    @autoreleasepool {
        UIView* view = (__bridge UIView*)ui_view;
        if (!view) return nullptr;

        CAEAGLLayer* eaglLayer = (CAEAGLLayer*)view.layer;
        if (![eaglLayer isKindOfClass:[CAEAGLLayer class]]) {
            return nullptr;
        }

        eaglLayer.opaque = YES;

        NSString* colorFormat = kEAGLColorFormatRGBA8;
        if (config.red_bits == 5 && config.green_bits == 6 && config.blue_bits == 5) {
            colorFormat = kEAGLColorFormatRGB565;
        }

        eaglLayer.drawableProperties = @{
            kEAGLDrawablePropertyRetainedBacking: @NO,
            kEAGLDrawablePropertyColorFormat: colorFormat
        };

        // Get shared context if provided
        EAGLContext* shared_context = nil;
        if (config.shared_graphics && config.shared_graphics->get_backend() == Backend::OpenGL) {
            shared_context = (__bridge EAGLContext*)config.shared_graphics->native_context();
        }

        EAGLContext* gl_context = nil;
        if (shared_context) {
            gl_context = [[EAGLContext alloc] initWithAPI:[shared_context API] sharegroup:[shared_context sharegroup]];
        } else {
            gl_context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
            if (!gl_context) {
                gl_context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
            }
        }

        if (!gl_context || ![EAGLContext setCurrentContext:gl_context]) {
            return nullptr;
        }

        GLuint framebuffer, color_renderbuffer, depth_renderbuffer;

        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        glGenRenderbuffers(1, &color_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, color_renderbuffer);
        [gl_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:eaglLayer];
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_renderbuffer);

        GLint rb_width, rb_height;
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &rb_width);
        glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &rb_height);

        glGenRenderbuffers(1, &depth_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer);

        GLenum depthFormat = GL_DEPTH24_STENCIL8;
        if (config.depth_bits == 16 && config.stencil_bits == 0) {
            depthFormat = GL_DEPTH_COMPONENT16;
        } else if (config.depth_bits == 24 && config.stencil_bits == 0) {
            depthFormat = GL_DEPTH_COMPONENT24;
        }

        glRenderbufferStorage(GL_RENDERBUFFER, depthFormat, rb_width, rb_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_renderbuffer);
        if (config.stencil_bits > 0) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_renderbuffer);
        }

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &framebuffer);
            glDeleteRenderbuffers(1, &color_renderbuffer);
            glDeleteRenderbuffers(1, &depth_renderbuffer);
            [EAGLContext setCurrentContext:nil];
            return nullptr;
        }

        GraphicsOpenGLES* gfx = new GraphicsOpenGLES();
        gfx->context = gl_context;
        gfx->framebuffer = framebuffer;
        gfx->color_renderbuffer = color_renderbuffer;
        gfx->depth_renderbuffer = depth_renderbuffer;
        gfx->device_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

        return gfx;
    }
}

#endif // TARGET_OS_IOS || TARGET_OS_TV

} // namespace window

#endif // __APPLE__ && !WINDOW_NO_OPENGL
