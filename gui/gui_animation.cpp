/*
 * gui_animation.cpp - Animation and AnimationManager Implementations
 *
 * Note: apply_easing() and string conversion functions are in gui.cpp.
 */

#include "gui_widget_base.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace window {
namespace gui {

// ============================================================================
// GuiAnimation
// ============================================================================

class GuiAnimation : public IGuiAnimation {
    int id_=-1;
    std::string name_;
    IGuiWidget* target_=nullptr;
    AnimationTarget target_prop_=AnimationTarget::Opacity;
    std::vector<AnimationKeyframe> keyframes_;
    float duration_=0, delay_=0, current_time_=0, speed_=1.0f;
    AnimationEasing easing_=AnimationEasing::Linear;
    AnimationLoop loop_mode_=AnimationLoop::None;
    int loop_count_=0, current_loop_=0;
    AnimationState state_=AnimationState::Idle;
    math::Vec4 start_val_, end_val_, current_val_;
    bool auto_destroy_=false, use_keyframes_=false;
    IAnimationEventHandler* handler_=nullptr;
public:
    explicit GuiAnimation(int id) : id_(id) {}

    int get_id() const override { return id_; }
    const char* get_name() const override { return name_.c_str(); }
    void set_name(const char* n) override { name_=n?n:""; }
    IGuiWidget* get_target() const override { return target_; }
    void set_target(IGuiWidget* w) override { target_=w; }
    AnimationTarget get_target_property() const override { return target_prop_; }
    void set_target_property(AnimationTarget t) override { target_prop_=t; }

    void animate_to(const math::Vec4& end, float dur) override {
        start_val_=current_val_; end_val_=end; duration_=dur; use_keyframes_=false;
    }
    void animate_from_to(const math::Vec4& start, const math::Vec4& end, float dur) override {
        start_val_=start; end_val_=end; duration_=dur; use_keyframes_=false;
    }

    void clear_keyframes() override { keyframes_.clear(); use_keyframes_=false; }
    void add_keyframe(const AnimationKeyframe& kf) override {
        keyframes_.push_back(kf);
        std::sort(keyframes_.begin(),keyframes_.end(),[](const auto& a,const auto& b){return a.time<b.time;});
        if(!keyframes_.empty()) duration_=keyframes_.back().time;
        use_keyframes_=true;
    }
    int get_keyframe_count() const override { return (int)keyframes_.size(); }
    const AnimationKeyframe* get_keyframe(int idx) const override { return (idx>=0&&idx<(int)keyframes_.size())?&keyframes_[idx]:nullptr; }

    float get_duration() const override { return duration_; }
    void set_duration(float d) override { duration_=d; }
    float get_delay() const override { return delay_; }
    void set_delay(float d) override { delay_=d; }
    AnimationEasing get_easing() const override { return easing_; }
    void set_easing(AnimationEasing e) override { easing_=e; }
    AnimationLoop get_loop_mode() const override { return loop_mode_; }
    void set_loop_mode(AnimationLoop m) override { loop_mode_=m; }
    int get_loop_count() const override { return loop_count_; }
    void set_loop_count(int c) override { loop_count_=c; }
    int get_current_loop() const override { return current_loop_; }

    void start() override { state_=AnimationState::Playing; current_time_=-delay_; current_loop_=0; if(handler_) handler_->on_animation_started(id_); }
    void pause() override { if(state_==AnimationState::Playing){state_=AnimationState::Paused; if(handler_) handler_->on_animation_paused(id_);} }
    void resume() override { if(state_==AnimationState::Paused){state_=AnimationState::Playing; if(handler_) handler_->on_animation_resumed(id_);} }
    void stop() override { state_=AnimationState::Idle; current_time_=0; }
    void reset() override { current_time_=-delay_; current_loop_=0; current_val_=start_val_; }

    AnimationState get_state() const override { return state_; }
    float get_current_time() const override { return std::max(0.0f,current_time_); }
    float get_progress() const override { return duration_>0?std::clamp(current_time_/duration_,0.0f,1.0f):1.0f; }
    math::Vec4 get_current_value() const override { return current_val_; }
    float get_speed() const override { return speed_; }
    void set_speed(float s) override { speed_=s; }
    bool is_auto_destroy() const override { return auto_destroy_; }
    void set_auto_destroy(bool a) override { auto_destroy_=a; }
    void set_animation_event_handler(IAnimationEventHandler* h) override { handler_=h; }

    void update(float dt) {
        if(state_!=AnimationState::Playing) return;
        current_time_+=dt*speed_;
        if(current_time_<0) return;

        float t=duration_>0?current_time_/duration_:1.0f;
        if(t>=1.0f) {
            if(loop_mode_==AnimationLoop::None||(loop_count_>0&&current_loop_>=loop_count_-1)){
                t=1.0f; state_=AnimationState::Completed;
                evaluate(t); if(handler_) handler_->on_animation_completed(id_);
                return;
            }
            ++current_loop_; if(handler_) handler_->on_animation_looped(id_,current_loop_);
            if(loop_mode_==AnimationLoop::PingPong) speed_=-speed_;
            current_time_=0; t=0;
        }
        evaluate(t);
    }

private:
    void evaluate(float t) {
        if(use_keyframes_&&keyframes_.size()>=2){
            float time=t*duration_;
            int seg=0;
            for(int i=0;i<(int)keyframes_.size()-1;++i) if(time>=keyframes_[i].time) seg=i;
            float seg_start=keyframes_[seg].time, seg_end=keyframes_[seg+1].time;
            float local_t=(seg_end>seg_start)?(time-seg_start)/(seg_end-seg_start):1.0f;
            float eased=apply_easing(keyframes_[seg].easing,local_t);
            auto& a=keyframes_[seg].value; auto& b=keyframes_[seg+1].value;
            current_val_={a.x+(b.x-a.x)*eased, a.y+(b.y-a.y)*eased, a.z+(b.z-a.z)*eased, a.w+(b.w-a.w)*eased};
        } else {
            float eased=apply_easing(easing_,t);
            current_val_={start_val_.x+(end_val_.x-start_val_.x)*eased, start_val_.y+(end_val_.y-start_val_.y)*eased,
                          start_val_.z+(end_val_.z-start_val_.z)*eased, start_val_.w+(end_val_.w-start_val_.w)*eased};
        }
    }
};

// ============================================================================
// GuiAnimationManager
// ============================================================================

class GuiAnimationManager : public IGuiAnimationManager {
    std::vector<std::unique_ptr<GuiAnimation>> anims_;
    int next_id_=0;
    float global_speed_=1.0f;
public:
    void update(float dt) override {
        float scaled_dt=dt*global_speed_;
        for(auto& a:anims_) a->update(scaled_dt);
        anims_.erase(std::remove_if(anims_.begin(),anims_.end(),[](const auto& a){
            return a->get_state()==AnimationState::Completed && a->is_auto_destroy();
        }),anims_.end());
    }
    IGuiAnimation* create_animation() override {
        auto a=std::make_unique<GuiAnimation>(next_id_++);
        auto* ptr=a.get(); anims_.push_back(std::move(a)); return ptr;
    }
    void destroy_animation(IGuiAnimation* anim) override {
        anims_.erase(std::remove_if(anims_.begin(),anims_.end(),[anim](const auto& a){return a.get()==anim;}),anims_.end());
    }
    void destroy_animation(int id) override {
        anims_.erase(std::remove_if(anims_.begin(),anims_.end(),[id](const auto& a){return a->get_id()==id;}),anims_.end());
    }
    IGuiAnimation* get_animation(int id) const override {
        for(auto& a:anims_) if(a->get_id()==id) return a.get();
        return nullptr;
    }
    IGuiAnimation* get_animation(const char* name) const override {
        if(!name) return nullptr;
        for(auto& a:anims_) if(std::strcmp(a->get_name(),name)==0) return a.get();
        return nullptr;
    }
    int get_animations_for_widget(IGuiWidget* widget, IGuiAnimation** out, int max) const override {
        if(!out||max<=0) return 0;
        int n=0;
        for(auto& a:anims_) if(a->get_target()==widget&&n<max) out[n++]=a.get();
        return n;
    }
    void pause_all() override { for(auto& a:anims_) a->pause(); }
    void resume_all() override { for(auto& a:anims_) a->resume(); }
    void stop_all() override { for(auto& a:anims_) a->stop(); }
    void stop_animations_for_widget(IGuiWidget* widget) override {
        for(auto& a:anims_) if(a->get_target()==widget) a->stop();
    }
    int get_animation_count() const override { return (int)anims_.size(); }
    int get_active_animation_count() const override {
        int n=0; for(auto& a:anims_) if(a->get_state()==AnimationState::Playing) ++n;
        return n;
    }
    float get_global_speed() const override { return global_speed_; }
    void set_global_speed(float s) override { global_speed_=s; }
};

// Factory function
IGuiAnimationManager* create_animation_manager_widget() { return new GuiAnimationManager(); }

} // namespace gui
} // namespace window
