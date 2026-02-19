/*
 * gui_page.cpp - Page and PageView Implementations
 */

#include "gui_widget_base.hpp"
#include <cstring>

namespace window {
namespace gui {

class GuiPage : public WidgetBase<IGuiPage, WidgetType::Custom> {
    std::string page_id_, page_title_;
    PageState state_=PageState::Hidden;
    IGuiWidget *content_=nullptr, *header_=nullptr, *footer_=nullptr;
    void* user_data_=nullptr;
    std::unordered_map<std::string,std::string> params_;
    PageStyle style_=PageStyle::default_style();
    PageTransition enter_trans_, exit_trans_;
    IPageEventHandler* handler_=nullptr;
    bool modal_=false;
public:
    const char* get_page_id() const override { return page_id_.c_str(); }
    void set_page_id(const char* id) override { page_id_=id?id:""; }
    const char* get_page_title() const override { return page_title_.c_str(); }
    void set_page_title(const char* t) override { page_title_=t?t:""; }
    PageState get_page_state() const override { return state_; }
    IGuiWidget* get_content() const override { return content_; }
    void set_content(IGuiWidget* c) override { content_=c; }
    IGuiWidget* get_header() const override { return header_; }
    void set_header(IGuiWidget* h) override { header_=h; }
    IGuiWidget* get_footer() const override { return footer_; }
    void set_footer(IGuiWidget* f) override { footer_=f; }
    void* get_user_data() const override { return user_data_; }
    void set_user_data(void* d) override { user_data_=d; }
    const char* get_parameter(const char* key) const override {
        if(!key) return ""; auto it=params_.find(key); return it!=params_.end()?it->second.c_str():"";
    }
    void set_parameter(const char* key, const char* val) override { if(key) params_[key]=val?val:""; }
    void clear_parameters() override { params_.clear(); }
    const PageStyle& get_page_style() const override { return style_; }
    void set_page_style(const PageStyle& s) override { style_=s; }
    PageTransition get_enter_transition() const override { return enter_trans_; }
    void set_enter_transition(const PageTransition& t) override { enter_trans_=t; }
    PageTransition get_exit_transition() const override { return exit_trans_; }
    void set_exit_transition(const PageTransition& t) override { exit_trans_=t; }
    void set_page_event_handler(IPageEventHandler* h) override { handler_=h; }
    bool is_modal() const override { return modal_; }
    void set_modal(bool m) override { modal_=m; }
    bool can_go_back() const override { return false; }
    // Internal state setter used by PageView
    void set_page_state(PageState s) { state_=s; }
    IPageEventHandler* get_handler() const { return handler_; }
};

// ============================================================================
// GuiPageView
// ============================================================================

class GuiPageView : public WidgetBase<IGuiPageView, WidgetType::Custom> {
    std::vector<IGuiPage*> pages_;
    std::vector<IGuiPage*> stack_;
    IGuiPage* modal_page_=nullptr;
    int current_idx_=-1;
    bool transitioning_=false;
    float trans_progress_=0;
    PageTransition default_push_=PageTransition::slide_left();
    PageTransition default_pop_=PageTransition::slide_right();
    bool gesture_nav_=true;
    std::vector<IGuiPage*> history_;
    int history_pos_=-1;
    IPageViewEventHandler* handler_=nullptr;

    void set_current(IGuiPage* page) {
        if(current_idx_>=0 && current_idx_<(int)pages_.size()){
            auto* old=pages_[current_idx_];
            if(auto* gp=dynamic_cast<GuiPage*>(old)) gp->set_page_state(PageState::Hidden);
        }
        for(int i=0;i<(int)pages_.size();++i) if(pages_[i]==page){current_idx_=i;break;}
        if(auto* gp=dynamic_cast<GuiPage*>(page)) gp->set_page_state(PageState::Active);
    }
public:
    int get_page_count() const override { return (int)pages_.size(); }
    IGuiPage* get_page(int idx) const override { return (idx>=0&&idx<(int)pages_.size())?pages_[idx]:nullptr; }
    IGuiPage* get_page_by_id(const char* id) const override {
        if(!id) return nullptr;
        for(auto* p:pages_) if(std::strcmp(p->get_page_id(),id)==0) return p;
        return nullptr;
    }
    int get_page_index(IGuiPage* page) const override {
        for(int i=0;i<(int)pages_.size();++i) if(pages_[i]==page) return i;
        return -1;
    }
    IGuiPage* get_current_page() const override { return (current_idx_>=0&&current_idx_<(int)pages_.size())?pages_[current_idx_]:nullptr; }
    int get_current_index() const override { return current_idx_; }
    void push_page(IGuiPage* page, const PageTransition&) override {
        if(!page) return;
        if(get_page_index(page)<0) pages_.push_back(page);
        stack_.push_back(page);
        set_current(page);
        history_.resize(history_pos_+1); history_.push_back(page); history_pos_=(int)history_.size()-1;
    }
    IGuiPage* pop_page(const PageTransition&) override {
        if(stack_.size()<=1) return nullptr;
        auto* popped=stack_.back(); stack_.pop_back();
        set_current(stack_.back());
        return popped;
    }
    void pop_to_page(IGuiPage* page, const PageTransition&) override {
        while(stack_.size()>1 && stack_.back()!=page) stack_.pop_back();
        if(!stack_.empty()) set_current(stack_.back());
    }
    void pop_to_root(const PageTransition&) override {
        while(stack_.size()>1) stack_.pop_back();
        if(!stack_.empty()) set_current(stack_.front());
    }
    int get_stack_depth() const override { return (int)stack_.size(); }
    bool can_pop() const override { return stack_.size()>1; }
    void set_page(IGuiPage* page, const PageTransition&) override {
        if(!page) return;
        if(get_page_index(page)<0) pages_.push_back(page);
        set_current(page);
    }
    void set_page_at_index(int idx, const PageTransition&) override {
        if(idx>=0&&idx<(int)pages_.size()) set_current(pages_[idx]);
    }
    void add_page(IGuiPage* page) override { if(page && get_page_index(page)<0) pages_.push_back(page); }
    void remove_page(IGuiPage* page) override {
        auto it=std::find(pages_.begin(),pages_.end(),page);
        if(it!=pages_.end()){int idx=(int)(it-pages_.begin()); pages_.erase(it); if(current_idx_==idx) current_idx_=pages_.empty()?-1:0;}
    }
    void remove_page_by_id(const char* id) override { auto* p=get_page_by_id(id); if(p) remove_page(p); }
    void clear_pages() override { pages_.clear(); stack_.clear(); current_idx_=-1; }
    void present_modal(IGuiPage* page, const PageTransition&) override { modal_page_=page; }
    void dismiss_modal(const PageTransition&) override { modal_page_=nullptr; }
    IGuiPage* get_presented_modal() const override { return modal_page_; }
    bool has_modal() const override { return modal_page_!=nullptr; }
    bool is_transitioning() const override { return transitioning_; }
    float get_transition_progress() const override { return trans_progress_; }
    void cancel_transition() override { transitioning_=false; trans_progress_=0; }
    PageTransition get_default_push_transition() const override { return default_push_; }
    void set_default_push_transition(const PageTransition& t) override { default_push_=t; }
    PageTransition get_default_pop_transition() const override { return default_pop_; }
    void set_default_pop_transition(const PageTransition& t) override { default_pop_=t; }
    bool is_gesture_navigation_enabled() const override { return gesture_nav_; }
    void set_gesture_navigation_enabled(bool e) override { gesture_nav_=e; }
    bool can_go_back() const override { return history_pos_>0; }
    bool can_go_forward() const override { return history_pos_<(int)history_.size()-1; }
    void go_back() override { if(can_go_back()){--history_pos_; set_current(history_[history_pos_]);} }
    void go_forward() override { if(can_go_forward()){++history_pos_; set_current(history_[history_pos_]);} }
    void clear_history() override { history_.clear(); history_pos_=-1; }
    void set_page_view_event_handler(IPageViewEventHandler* h) override { handler_=h; }
    void get_page_view_render_info(PageViewRenderInfo* out) const override {
        if(!out) return; auto b=base_.get_bounds();
        out->widget=this; out->bounds=b; out->clip_rect=base_.is_clip_enabled()?base_.get_clip_rect():b;
        out->current_page=get_current_page(); out->is_transitioning=transitioning_;
        out->transition_progress=trans_progress_;
        out->show_modal_overlay=(modal_page_!=nullptr);
    }
};

// Factory functions
IGuiPage* create_page_widget() { return new GuiPage(); }
IGuiPageView* create_page_view_widget() { return new GuiPageView(); }

} // namespace gui
} // namespace window
