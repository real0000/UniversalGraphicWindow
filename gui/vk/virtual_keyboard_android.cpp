/*
 * virtual_keyboard_android.cpp - Android Virtual Keyboard Implementation
 *
 * Uses InputMethodManager for soft keyboard control.
 * Requires JNI bridge to access Android APIs.
 */

#ifdef __ANDROID__

#include "virtual_keyboard.hpp"
#include <jni.h>
#include <android/native_activity.h>
#include <android/log.h>
#include <cstring>

#define LOG_TAG "VirtualKeyboard"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace vkeyboard {

using Box = window::math::Box;

// Global reference to JNI environment (set by application)
static JavaVM* g_java_vm = nullptr;
static jobject g_activity = nullptr;

// Set the Java VM and activity (call from JNI_OnLoad or main activity)
extern "C" void vkeyboard_set_android_context(JavaVM* vm, jobject activity) {
    g_java_vm = vm;
    g_activity = activity;
}

class VirtualKeyboardAndroid : public IVirtualKeyboard {
public:
    VirtualKeyboardAndroid();
    ~VirtualKeyboardAndroid();

    Result initialize() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    bool is_supported() const override { return true; }
    bool is_available() const override { return g_java_vm != nullptr; }

    Result show() override;
    Result show(const KeyboardConfig& config) override;
    Result hide() override;
    Result toggle() override;

    KeyboardState get_state() const override { return state_; }
    bool is_visible() const override;
    Box get_frame() const override { return frame_; }
    float get_height() const override { return window::math::box_height(frame_); }

    void set_config(const KeyboardConfig& config) override { config_ = config; }
    KeyboardConfig get_config() const override { return config_; }

    void set_text_input_delegate(ITextInputDelegate* delegate) override { text_delegate_ = delegate; }
    ITextInputDelegate* get_text_input_delegate() const override { return text_delegate_; }
    void update_text_input_context(const TextInputContext& context) override { text_context_ = context; }

    void begin_text_input() override { text_input_active_ = true; }
    void end_text_input() override { text_input_active_ = false; }
    bool is_text_input_active() const override { return text_input_active_; }

    void set_event_handler(IVirtualKeyboardEventHandler* handler) override { event_handler_ = handler; }

    Result get_available_layouts(KeyboardLayoutList* out_list) const override;
    Result get_current_layout(KeyboardLayoutInfo* out_info) const override;
    Result set_layout(const char* identifier) override;

    void* get_native_handle() const override { return nullptr; }

    void update() override;

    // JNI callbacks
    void on_keyboard_height_changed(float height);

private:
    JNIEnv* get_jni_env() const;
    bool show_soft_input();
    bool hide_soft_input();
    int get_keyboard_type_flag() const;

    bool initialized_ = false;
    KeyboardState state_ = KeyboardState::Hidden;
    Box frame_;
    KeyboardConfig config_;
    ITextInputDelegate* text_delegate_ = nullptr;
    IVirtualKeyboardEventHandler* event_handler_ = nullptr;
    TextInputContext text_context_;
    bool text_input_active_ = false;

    jclass input_method_manager_class_ = nullptr;
    jmethodID show_soft_input_method_ = nullptr;
    jmethodID hide_soft_input_method_ = nullptr;
};

VirtualKeyboardAndroid::VirtualKeyboardAndroid() {}

VirtualKeyboardAndroid::~VirtualKeyboardAndroid() {
    shutdown();
}

JNIEnv* VirtualKeyboardAndroid::get_jni_env() const {
    if (!g_java_vm) return nullptr;

    JNIEnv* env = nullptr;
    int status = g_java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        if (g_java_vm->AttachCurrentThread(&env, nullptr) != 0) {
            LOGE("Failed to attach thread to JVM");
            return nullptr;
        }
    } else if (status != JNI_OK) {
        LOGE("Failed to get JNI environment");
        return nullptr;
    }

    return env;
}

Result VirtualKeyboardAndroid::initialize() {
    if (initialized_) {
        return Result::ErrorAlreadyInitialized;
    }

    if (!g_java_vm) {
        LOGE("Java VM not set. Call vkeyboard_set_android_context first.");
        return Result::ErrorNotInitialized;
    }

    JNIEnv* env = get_jni_env();
    if (!env) {
        return Result::ErrorNotInitialized;
    }

    // Cache InputMethodManager class and methods
    jclass local_class = env->FindClass("android/view/inputmethod/InputMethodManager");
    if (local_class) {
        input_method_manager_class_ = (jclass)env->NewGlobalRef(local_class);
        env->DeleteLocalRef(local_class);
    }

    initialized_ = true;
    LOGI("Virtual keyboard initialized");
    return Result::Success;
}

void VirtualKeyboardAndroid::shutdown() {
    if (!initialized_) return;

    JNIEnv* env = get_jni_env();
    if (env && input_method_manager_class_) {
        env->DeleteGlobalRef(input_method_manager_class_);
        input_method_manager_class_ = nullptr;
    }

    initialized_ = false;
}

Result VirtualKeyboardAndroid::show() {
    return show(config_);
}

Result VirtualKeyboardAndroid::show(const KeyboardConfig& config) {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    config_ = config;

    if (show_soft_input()) {
        state_ = KeyboardState::Showing;

        if (event_handler_) {
            KeyboardEventData data;
            data.state = KeyboardState::Showing;
            data.previous_state = KeyboardState::Hidden;
            event_handler_->on_keyboard_will_show(data);
        }

        return Result::Success;
    }

    return Result::ErrorUnknown;
}

Result VirtualKeyboardAndroid::hide() {
    if (!initialized_) {
        return Result::ErrorNotInitialized;
    }

    if (hide_soft_input()) {
        state_ = KeyboardState::Hiding;

        if (event_handler_) {
            KeyboardEventData data;
            data.state = KeyboardState::Hiding;
            data.previous_state = KeyboardState::Visible;
            event_handler_->on_keyboard_will_hide(data);
        }

        return Result::Success;
    }

    return Result::ErrorUnknown;
}

Result VirtualKeyboardAndroid::toggle() {
    JNIEnv* env = get_jni_env();
    if (!env || !g_activity) {
        return Result::ErrorNotInitialized;
    }

    // Get InputMethodManager
    jclass context_class = env->FindClass("android/content/Context");
    jmethodID get_system_service = env->GetMethodID(context_class, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");

    jstring imm_service = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(g_activity, get_system_service, imm_service);
    env->DeleteLocalRef(imm_service);

    if (!imm) {
        env->DeleteLocalRef(context_class);
        return Result::ErrorUnknown;
    }

    // Call toggleSoftInput
    jclass imm_class = env->GetObjectClass(imm);
    jmethodID toggle_method = env->GetMethodID(imm_class, "toggleSoftInput", "(II)V");

    env->CallVoidMethod(imm, toggle_method, 0, 0);

    env->DeleteLocalRef(imm_class);
    env->DeleteLocalRef(imm);
    env->DeleteLocalRef(context_class);

    return Result::Success;
}

bool VirtualKeyboardAndroid::is_visible() const {
    return state_ == KeyboardState::Visible;
}

void VirtualKeyboardAndroid::update() {
    // State updates are handled via JNI callbacks
}

void VirtualKeyboardAndroid::on_keyboard_height_changed(float height) {
    KeyboardState old_state = state_;

    if (height > 0) {
        frame_ = window::math::make_box(0, 0, 0, height);
        state_ = KeyboardState::Visible;

        if (old_state != KeyboardState::Visible && event_handler_) {
            KeyboardEventData data;
            data.state = KeyboardState::Visible;
            data.previous_state = old_state;
            data.frame = frame_;
            event_handler_->on_keyboard_did_show(data);
        }
    } else {
        frame_ = Box(window::math::Vec2(0,0), window::math::Vec2(0,0));
        state_ = KeyboardState::Hidden;

        if (old_state != KeyboardState::Hidden && event_handler_) {
            KeyboardEventData data;
            data.state = KeyboardState::Hidden;
            data.previous_state = old_state;
            data.frame = frame_;
            event_handler_->on_keyboard_did_hide(data);
        }
    }
}

bool VirtualKeyboardAndroid::show_soft_input() {
    JNIEnv* env = get_jni_env();
    if (!env || !g_activity) {
        return false;
    }

    // Get InputMethodManager
    jclass context_class = env->FindClass("android/content/Context");
    jmethodID get_system_service = env->GetMethodID(context_class, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");

    jstring imm_service = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(g_activity, get_system_service, imm_service);
    env->DeleteLocalRef(imm_service);

    if (!imm) {
        env->DeleteLocalRef(context_class);
        return false;
    }

    // Get current focus view
    jclass activity_class = env->GetObjectClass(g_activity);
    jmethodID get_current_focus = env->GetMethodID(activity_class, "getCurrentFocus",
        "()Landroid/view/View;");
    jobject view = env->CallObjectMethod(g_activity, get_current_focus);

    bool result = false;

    if (view) {
        // Show soft input
        jclass imm_class = env->GetObjectClass(imm);
        jmethodID show_method = env->GetMethodID(imm_class, "showSoftInput",
            "(Landroid/view/View;I)Z");

        int flags = get_keyboard_type_flag();
        result = env->CallBooleanMethod(imm, show_method, view, flags);

        env->DeleteLocalRef(imm_class);
        env->DeleteLocalRef(view);
    } else {
        // No focused view - try to show anyway
        jclass imm_class = env->GetObjectClass(imm);
        jmethodID toggle_method = env->GetMethodID(imm_class, "toggleSoftInput", "(II)V");
        env->CallVoidMethod(imm, toggle_method, 2 /* SHOW_FORCED */, 0);
        result = true;
        env->DeleteLocalRef(imm_class);
    }

    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(imm);
    env->DeleteLocalRef(context_class);

    return result;
}

bool VirtualKeyboardAndroid::hide_soft_input() {
    JNIEnv* env = get_jni_env();
    if (!env || !g_activity) {
        return false;
    }

    // Get InputMethodManager
    jclass context_class = env->FindClass("android/content/Context");
    jmethodID get_system_service = env->GetMethodID(context_class, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");

    jstring imm_service = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(g_activity, get_system_service, imm_service);
    env->DeleteLocalRef(imm_service);

    if (!imm) {
        env->DeleteLocalRef(context_class);
        return false;
    }

    // Get window token
    jclass activity_class = env->GetObjectClass(g_activity);
    jmethodID get_window = env->GetMethodID(activity_class, "getWindow",
        "()Landroid/view/Window;");
    jobject window = env->CallObjectMethod(g_activity, get_window);

    bool result = false;

    if (window) {
        jclass window_class = env->GetObjectClass(window);
        jmethodID get_decor_view = env->GetMethodID(window_class, "getDecorView",
            "()Landroid/view/View;");
        jobject decor_view = env->CallObjectMethod(window, get_decor_view);

        if (decor_view) {
            jclass view_class = env->GetObjectClass(decor_view);
            jmethodID get_window_token = env->GetMethodID(view_class, "getWindowToken",
                "()Landroid/os/IBinder;");
            jobject token = env->CallObjectMethod(decor_view, get_window_token);

            if (token) {
                jclass imm_class = env->GetObjectClass(imm);
                jmethodID hide_method = env->GetMethodID(imm_class, "hideSoftInputFromWindow",
                    "(Landroid/os/IBinder;I)Z");

                result = env->CallBooleanMethod(imm, hide_method, token, 0);

                env->DeleteLocalRef(imm_class);
                env->DeleteLocalRef(token);
            }

            env->DeleteLocalRef(view_class);
            env->DeleteLocalRef(decor_view);
        }

        env->DeleteLocalRef(window_class);
        env->DeleteLocalRef(window);
    }

    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(imm);
    env->DeleteLocalRef(context_class);

    return result;
}

int VirtualKeyboardAndroid::get_keyboard_type_flag() const {
    // Map KeyboardType to Android InputType flags
    switch (config_.type) {
        case KeyboardType::Number:
            return 0x00000002; // TYPE_CLASS_NUMBER
        case KeyboardType::Phone:
            return 0x00000003; // TYPE_CLASS_PHONE
        case KeyboardType::Email:
            return 0x00000001 | 0x00000020; // TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_EMAIL_ADDRESS
        case KeyboardType::URL:
            return 0x00000001 | 0x00000010; // TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_URI
        case KeyboardType::Password:
            return 0x00000001 | 0x00000080; // TYPE_CLASS_TEXT | TYPE_TEXT_VARIATION_PASSWORD
        case KeyboardType::Decimal:
            return 0x00000002 | 0x00002000; // TYPE_CLASS_NUMBER | TYPE_NUMBER_FLAG_DECIMAL
        default:
            return 0x00000001; // TYPE_CLASS_TEXT
    }
}

Result VirtualKeyboardAndroid::get_available_layouts(KeyboardLayoutList* out_list) const {
    if (!out_list) {
        return Result::ErrorInvalidParameter;
    }

    out_list->layouts.clear();

    JNIEnv* env = get_jni_env();
    if (!env || !g_activity) {
        return Result::ErrorNotInitialized;
    }

    // Get InputMethodManager
    jclass context_class = env->FindClass("android/content/Context");
    jmethodID get_system_service = env->GetMethodID(context_class, "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;");

    jstring imm_service = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(g_activity, get_system_service, imm_service);
    env->DeleteLocalRef(imm_service);
    env->DeleteLocalRef(context_class);

    if (!imm) {
        return Result::ErrorUnknown;
    }

    // Get enabled input method list
    jclass imm_class = env->GetObjectClass(imm);
    jmethodID get_enabled = env->GetMethodID(imm_class, "getEnabledInputMethodList",
        "()Ljava/util/List;");
    jobject input_methods = env->CallObjectMethod(imm, get_enabled);

    if (input_methods) {
        jclass list_class = env->FindClass("java/util/List");
        jmethodID size_method = env->GetMethodID(list_class, "size", "()I");
        jmethodID get_method = env->GetMethodID(list_class, "get", "(I)Ljava/lang/Object;");

        int count = env->CallIntMethod(input_methods, size_method);

        for (int i = 0; i < count; ++i) {
            jobject input_method = env->CallObjectMethod(input_methods, get_method, i);

            if (input_method) {
                KeyboardLayoutInfo info;
                jclass im_class = env->GetObjectClass(input_method);

                // Get ID
                jmethodID get_id = env->GetMethodID(im_class, "getId", "()Ljava/lang/String;");
                jstring id_str = (jstring)env->CallObjectMethod(input_method, get_id);
                if (id_str) {
                    const char* id = env->GetStringUTFChars(id_str, nullptr);
                    info.identifier = id;
                    env->ReleaseStringUTFChars(id_str, id);
                    env->DeleteLocalRef(id_str);
                }

                // Get label
                jmethodID load_label = env->GetMethodID(im_class, "loadLabel",
                    "(Landroid/content/pm/PackageManager;)Ljava/lang/CharSequence;");
                jclass context_cls = env->FindClass("android/content/Context");
                jmethodID get_pm = env->GetMethodID(context_cls, "getPackageManager",
                    "()Landroid/content/pm/PackageManager;");
                jobject pm = env->CallObjectMethod(g_activity, get_pm);

                if (pm) {
                    jobject label = env->CallObjectMethod(input_method, load_label, pm);
                    if (label) {
                        jclass cs_class = env->FindClass("java/lang/CharSequence");
                        jmethodID to_string = env->GetMethodID(cs_class, "toString",
                            "()Ljava/lang/String;");
                        jstring label_str = (jstring)env->CallObjectMethod(label, to_string);

                        if (label_str) {
                            const char* name = env->GetStringUTFChars(label_str, nullptr);
                            info.display_name = name;
                            env->ReleaseStringUTFChars(label_str, name);
                            env->DeleteLocalRef(label_str);
                        }

                        env->DeleteLocalRef(cs_class);
                        env->DeleteLocalRef(label);
                    }
                    env->DeleteLocalRef(pm);
                }

                env->DeleteLocalRef(context_cls);
                env->DeleteLocalRef(im_class);
                env->DeleteLocalRef(input_method);

                out_list->layouts.push_back(std::move(info));
            }
        }

        env->DeleteLocalRef(list_class);
        env->DeleteLocalRef(input_methods);
    }

    env->DeleteLocalRef(imm_class);
    env->DeleteLocalRef(imm);

    return Result::Success;
}

Result VirtualKeyboardAndroid::get_current_layout(KeyboardLayoutInfo* out_info) const {
    // Android doesn't have a direct API to get current input method
    // Return a placeholder
    if (!out_info) {
        return Result::ErrorInvalidParameter;
    }

    strncpy(out_info->display_name, "System Keyboard", sizeof(out_info->display_name) - 1);
    out_info->is_current = true;

    return Result::Success;
}

Result VirtualKeyboardAndroid::set_layout(const char* identifier) {
    // Android doesn't allow programmatic input method switching
    (void)identifier;
    return Result::ErrorNotSupported;
}

// Factory functions
IVirtualKeyboard* create_virtual_keyboard() {
    return new VirtualKeyboardAndroid();
}

void destroy_virtual_keyboard(IVirtualKeyboard* keyboard) {
    delete keyboard;
}

} // namespace vkeyboard

#endif // __ANDROID__
