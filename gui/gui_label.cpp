/*
 * gui_label.cpp - Label, TextInput, and EditBox Implementations
 */

#include "gui_widget_base.hpp"
#include <algorithm>
#include <sstream>

namespace window {
namespace gui {

// ============================================================================
// GuiLabel
// ============================================================================

class GuiLabel : public WidgetBase<IGuiLabel, WidgetType::Label> {
    std::string text_;
    LabelStyle label_style_ = LabelStyle::default_style();
public:
    const char* get_text() const override { return text_.c_str(); }
    void set_text(const char* t) override { text_ = t ? t : ""; }
    const LabelStyle& get_label_style() const override { return label_style_; }
    void set_label_style(const LabelStyle& s) override { label_style_ = s; }
};

// ============================================================================
// GuiTextInput
// ============================================================================

class GuiTextInput : public WidgetBase<IGuiTextInput, WidgetType::TextInput> {
    std::string text_, placeholder_;
    LabelStyle label_style_ = LabelStyle::default_style();
    int cursor_ = 0, sel_start_ = 0, sel_len_ = 0, max_length_ = 0;
    bool password_ = false, read_only_ = false;
    // Key codes from window::Key enum
    enum : int { K_Enter=308, K_Backspace=309, K_Delete=310,
                 K_Home=312, K_End=313, K_Left=316, K_Right=317 };
public:
    bool is_focusable() const override { return true; }
    bool handle_text_input(const char* t) override {
        if (!read_only_ && t) {
            if (sel_len_ > 0) delete_selection();
            std::string s(t);
            if (max_length_ > 0 && (int)(text_.size() + s.size()) > max_length_) return false;
            text_.insert(cursor_, s); cursor_ += (int)s.size();
        }
        return true;
    }
    bool handle_key(int code, bool pressed, int) override {
        if (!pressed) return false;
        switch (code) {
            case K_Left:      if (cursor_ > 0) --cursor_; return true;
            case K_Right:     if (cursor_ < (int)text_.size()) ++cursor_; return true;
            case K_Home:      cursor_ = 0; return true;
            case K_End:       cursor_ = (int)text_.size(); return true;
            case K_Backspace: if (!read_only_) { if (sel_len_ > 0) delete_selection(); else delete_backward(1); } return true;
            case K_Delete:    if (!read_only_) { if (sel_len_ > 0) delete_selection(); else delete_forward(1); } return true;
        }
        return false;
    }
    const char* get_text() const override { return text_.c_str(); }
    void set_text(const char* t) override { text_ = t ? t : ""; cursor_ = std::min(cursor_, (int)text_.size()); }
    const LabelStyle& get_label_style() const override { return label_style_; }
    void set_label_style(const LabelStyle& s) override { label_style_ = s; }
    int get_cursor_position() const override { return cursor_; }
    void set_cursor_position(int p) override { cursor_ = std::max(0, std::min(p, (int)text_.size())); }
    int get_selection_start() const override { return sel_start_; }
    int get_selection_length() const override { return sel_len_; }
    void set_selection(int s, int l) override { sel_start_ = s; sel_len_ = l; }
    void select_all() override { sel_start_ = 0; sel_len_ = (int)text_.size(); }
    void clear_selection() override { sel_start_ = 0; sel_len_ = 0; }
    void insert_text(const char* t) override {
        if (!t || read_only_) return; delete_selection();
        std::string s(t); text_.insert(cursor_, s); cursor_ += (int)s.size();
    }
    void delete_selection() override {
        if (sel_len_ > 0) { text_.erase(sel_start_, sel_len_); cursor_ = sel_start_; sel_len_ = 0; }
    }
    void delete_backward(int n) override {
        if (cursor_ > 0 && n > 0) { int d = std::min(n, cursor_); text_.erase(cursor_ - d, d); cursor_ -= d; }
    }
    void delete_forward(int n) override {
        if (cursor_ < (int)text_.size() && n > 0) { int d = std::min(n, (int)text_.size() - cursor_); text_.erase(cursor_, d); }
    }
    const char* get_placeholder() const override { return placeholder_.c_str(); }
    void set_placeholder(const char* p) override { placeholder_ = p ? p : ""; }
    bool is_password_mode() const override { return password_; }
    void set_password_mode(bool e) override { password_ = e; }
    bool is_read_only() const override { return read_only_; }
    void set_read_only(bool r) override { read_only_ = r; }
    int get_max_length() const override { return max_length_; }
    void set_max_length(int m) override { max_length_ = m; }
};

// ============================================================================
// GuiEditBox
// ============================================================================

class GuiEditBox : public WidgetBase<IGuiEditBox, WidgetType::Custom> {
    std::vector<std::string> lines_{""};
    mutable std::string cached_text_, cached_sel_;
    TextPosition cursor_{0,0};
    TextRange selection_{{0,0},{0,0}};
    EditBoxStyle style_ = EditBoxStyle::default_style();
    IEditBoxEventHandler* handler_ = nullptr;
    EditBoxWordWrap wrap_ = EditBoxWordWrap::None;
    bool line_nums_=true, hl_line_=true, read_only_=false, tab_spaces_=true;
    int tab_size_=4, first_vis_=0;
    // Key codes from window::Key enum
    enum : int { K_Tab=301, K_Enter=308, K_Backspace=309, K_Delete=310,
                 K_Home=312, K_End=313, K_Left=316, K_Right=317,
                 K_Up=318, K_Down=319 };
    void clamp(TextPosition& p) const {
        p.line=std::max(0,std::min(p.line,(int)lines_.size()-1));
        p.column=std::max(0,std::min(p.column,(int)lines_[p.line].size()));
    }
    void begin_or_extend_selection() {
        if (!has_selection()) { selection_.start = cursor_; selection_.end = cursor_; }
    }
public:
    bool is_focusable() const override { return true; }
    bool handle_text_input(const char* t) override {
        if (read_only_ || !t || !*t) return false;
        if (has_selection()) delete_selection();
        insert_text(t);
        return true;
    }
    bool handle_key(int code, bool pressed, int mods) override {
        if (!pressed) return false;
        bool shift = (mods & 1) != 0;
        bool ctrl = (mods & 2) != 0;
        // Ctrl+A: select all
        if (ctrl && (code == 'A' || code == 'a')) { select_all(); return true; }
        switch (code) {
            case K_Left:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.column > 0) --cursor_.column;
                else if (cursor_.line > 0) { --cursor_.line; cursor_.column = (int)lines_[cursor_.line].size(); }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Right:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.column < (int)lines_[cursor_.line].size()) ++cursor_.column;
                else if (cursor_.line < (int)lines_.size()-1) { ++cursor_.line; cursor_.column = 0; }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Up:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.line > 0) { --cursor_.line; clamp(cursor_); }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Down:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                if (cursor_.line < (int)lines_.size()-1) { ++cursor_.line; clamp(cursor_); }
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Home:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                cursor_.column = 0;
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_End:
                if (shift) { begin_or_extend_selection(); }
                else { clear_selection(); }
                cursor_.column = (int)lines_[cursor_.line].size();
                if (shift) { selection_.end = cursor_; }
                return true;
            case K_Backspace:
                if (!read_only_) { if (has_selection()) delete_selection(); else delete_backward(1); }
                return true;
            case K_Delete:
                if (!read_only_) { if (has_selection()) delete_selection(); else delete_forward(1); }
                return true;
            case K_Enter:
                if (!read_only_) {
                    if (has_selection()) delete_selection();
                    // Split line at cursor
                    std::string after = lines_[cursor_.line].substr(cursor_.column);
                    lines_[cursor_.line].erase(cursor_.column);
                    lines_.insert(lines_.begin() + cursor_.line + 1, after);
                    ++cursor_.line; cursor_.column = 0;
                }
                return true;
            case K_Tab:
                if (!read_only_) {
                    if (tab_spaces_) { std::string sp(tab_size_, ' '); insert_text(sp.c_str()); }
                    else insert_text("\t");
                }
                return true;
        }
        return false;
    }
    bool handle_mouse_scroll(float, float dy) override {
        int step = (dy > 0) ? -3 : 3;
        set_first_visible_line(first_vis_ + step);
        return true;
    }
    const char* get_text() const override {
        cached_text_.clear();
        for(int i=0;i<(int)lines_.size();++i){if(i)cached_text_+='\n';cached_text_+=lines_[i];}
        return cached_text_.c_str();
    }
    void set_text(const char* t) override {
        lines_.clear();
        if(!t||!*t){lines_.push_back("");return;}
        std::istringstream ss(t); std::string l;
        while(std::getline(ss,l)) lines_.push_back(l);
        if(lines_.empty()) lines_.push_back("");
        cursor_={0,0}; selection_={{0,0},{0,0}};
    }
    int get_text_length() const override {
        int n=0; for(auto& l:lines_) n+=(int)l.size(); return n+std::max(0,(int)lines_.size()-1);
    }
    int get_line_count() const override { return (int)lines_.size(); }
    const char* get_line(int i) const override { return (i>=0&&i<(int)lines_.size())?lines_[i].c_str():""; }
    int get_line_length(int i) const override { return (i>=0&&i<(int)lines_.size())?(int)lines_[i].size():0; }
    void insert_line(int i,const char* t) override {
        if(i<0)i=0; if(i>(int)lines_.size())i=(int)lines_.size();
        lines_.insert(lines_.begin()+i,t?t:"");
    }
    void remove_line(int i) override { if(i>=0&&i<(int)lines_.size()&&lines_.size()>1) lines_.erase(lines_.begin()+i); }
    void replace_line(int i,const char* t) override { if(i>=0&&i<(int)lines_.size()) lines_[i]=t?t:""; }
    TextPosition get_cursor_position() const override { return cursor_; }
    void set_cursor_position(const TextPosition& p) override { cursor_=p; clamp(cursor_); }
    void move_cursor(int dl,int dc) override { cursor_.line+=dl; cursor_.column+=dc; clamp(cursor_); }
    void move_cursor_to_line_start() override { cursor_.column=0; }
    void move_cursor_to_line_end() override { cursor_.column=(int)lines_[cursor_.line].size(); }
    void move_cursor_to_start() override { cursor_={0,0}; }
    void move_cursor_to_end() override { cursor_.line=(int)lines_.size()-1; cursor_.column=(int)lines_[cursor_.line].size(); }
    TextRange get_selection() const override { return selection_; }
    void set_selection(const TextRange& r) override { selection_=r; clamp(selection_.start); clamp(selection_.end); }
    void select_all() override { selection_.start={0,0}; selection_.end={(int)lines_.size()-1,(int)lines_.back().size()}; }
    void select_line(int i) override { if(i>=0&&i<(int)lines_.size()){selection_.start={i,0};selection_.end={i,(int)lines_[i].size()};} }
    void select_word_at_cursor() override {}
    void clear_selection() override { selection_={{0,0},{0,0}}; }
    bool has_selection() const override { return !selection_.is_empty(); }
    const char* get_selected_text() const override {
        if(!has_selection()) return "";
        auto s=selection_.start, e=selection_.end;
        if(s.line>e.line||(s.line==e.line&&s.column>e.column)) std::swap(s,e);
        cached_sel_.clear();
        if(s.line==e.line) cached_sel_=lines_[s.line].substr(s.column,e.column-s.column);
        else {
            cached_sel_=lines_[s.line].substr(s.column);
            for(int i=s.line+1;i<e.line;++i){cached_sel_+='\n';cached_sel_+=lines_[i];}
            cached_sel_+='\n'; cached_sel_+=lines_[e.line].substr(0,e.column);
        }
        return cached_sel_.c_str();
    }
    void insert_text(const char* t) override { insert_text_at(cursor_,t); }
    void insert_text_at(const TextPosition& pos,const char* t) override {
        if(!t||read_only_) return;
        TextPosition p=pos; clamp(p);
        std::string after=lines_[p.line].substr(p.column);
        lines_[p.line].erase(p.column);
        std::istringstream ss(t); std::string part; bool first=true; int nl=p.line;
        while(std::getline(ss,part)){
            if(first){lines_[nl]+=part;first=false;}
            else{lines_.insert(lines_.begin()+(++nl),part);}
        }
        lines_[nl]+=after;
        cursor_={nl,(int)(lines_[nl].size()-after.size())};
    }
    void delete_selection() override {
        if(!has_selection()||read_only_) return;
        auto s=selection_.start, e=selection_.end;
        if(s.line>e.line||(s.line==e.line&&s.column>e.column)) std::swap(s,e);
        if(s.line==e.line) lines_[s.line].erase(s.column,e.column-s.column);
        else {
            lines_[s.line].erase(s.column);
            lines_[s.line]+=lines_[e.line].substr(e.column);
            lines_.erase(lines_.begin()+s.line+1,lines_.begin()+e.line+1);
        }
        cursor_=s; clear_selection();
    }
    void delete_range(const TextRange& r) override { selection_=r; delete_selection(); }
    void delete_backward(int n) override {
        if(read_only_) return;
        for(int i=0;i<n&&(cursor_.line>0||cursor_.column>0);++i){
            if(cursor_.column>0){lines_[cursor_.line].erase(--cursor_.column,1);}
            else if(cursor_.line>0){cursor_.column=(int)lines_[cursor_.line-1].size();lines_[cursor_.line-1]+=lines_[cursor_.line];lines_.erase(lines_.begin()+cursor_.line);--cursor_.line;}
        }
    }
    void delete_forward(int n) override {
        if(read_only_) return;
        for(int i=0;i<n;++i){
            if(cursor_.column<(int)lines_[cursor_.line].size()) lines_[cursor_.line].erase(cursor_.column,1);
            else if(cursor_.line<(int)lines_.size()-1){lines_[cursor_.line]+=lines_[cursor_.line+1];lines_.erase(lines_.begin()+cursor_.line+1);}
        }
    }
    void delete_line(int i) override { remove_line(i); }
    void duplicate_line() override { if(cursor_.line<(int)lines_.size()) lines_.insert(lines_.begin()+cursor_.line+1,lines_[cursor_.line]); }
    void move_line_up() override { if(cursor_.line>0){std::swap(lines_[cursor_.line],lines_[cursor_.line-1]);--cursor_.line;} }
    void move_line_down() override { if(cursor_.line<(int)lines_.size()-1){std::swap(lines_[cursor_.line],lines_[cursor_.line+1]);++cursor_.line;} }
    bool can_undo() const override { return false; }
    bool can_redo() const override { return false; }
    void undo() override {}
    void redo() override {}
    void clear_undo_history() override {}
    int get_undo_stack_size() const override { return 0; }
    void cut() override { copy(); delete_selection(); }
    void copy() override {}
    void paste() override {}
    TextPosition find(const char* text,const TextPosition& start,bool cs,bool) const override {
        if(!text||!*text) return {-1,-1};
        std::string needle(text);
        for(int i=start.line;i<(int)lines_.size();++i){
            int col=(i==start.line)?start.column:0;
            size_t pos;
            if(cs) pos=lines_[i].find(needle,col);
            else{std::string ll=lines_[i],ln=needle;std::transform(ll.begin(),ll.end(),ll.begin(),::tolower);std::transform(ln.begin(),ln.end(),ln.begin(),::tolower);pos=ll.find(ln,col);}
            if(pos!=std::string::npos) return {i,(int)pos};
        }
        return {-1,-1};
    }
    int replace(const char* s,const char* r,bool cs,bool ww) override {
        auto pos=find(s,cursor_,cs,ww); if(pos.line<0) return 0;
        cursor_=pos; selection_.start=pos; selection_.end={pos.line,pos.column+(int)std::strlen(s)};
        delete_selection(); insert_text(r); return 1;
    }
    int replace_all(const char* s,const char* r,bool cs,bool ww) override {
        int cnt=0; TextPosition p={0,0};
        while(true){p=find(s,p,cs,ww);if(p.line<0)break;cursor_=p;selection_.start=p;selection_.end={p.line,p.column+(int)std::strlen(s)};delete_selection();insert_text(r);p=cursor_;++cnt;}
        return cnt;
    }
    EditBoxWordWrap get_word_wrap() const override { return wrap_; }
    void set_word_wrap(EditBoxWordWrap w) override { wrap_=w; }
    bool is_line_numbers_visible() const override { return line_nums_; }
    void set_line_numbers_visible(bool v) override { line_nums_=v; }
    bool is_current_line_highlighted() const override { return hl_line_; }
    void set_current_line_highlighted(bool h) override { hl_line_=h; }
    bool is_read_only() const override { return read_only_; }
    void set_read_only(bool r) override { read_only_=r; }
    bool is_tab_insert_spaces() const override { return tab_spaces_; }
    void set_tab_insert_spaces(bool s) override { tab_spaces_=s; }
    int get_tab_size() const override { return tab_size_; }
    void set_tab_size(int s) override { tab_size_=s; }
    int get_first_visible_line() const override { return first_vis_; }
    void set_first_visible_line(int l) override { first_vis_=std::max(0,std::min(l,(int)lines_.size()-1)); }
    int get_visible_line_count() const override {
        float h=math::box_height(base_.get_bounds()); float lh=style_.font_size*style_.line_height;
        return lh>0?(int)(h/lh):0;
    }
    void scroll_to_cursor() override { set_first_visible_line(cursor_.line); }
    void scroll_to_line(int l) override { set_first_visible_line(l); }
    TextPosition position_from_point(const math::Vec2&) const override { return cursor_; }
    math::Vec2 point_from_position(const TextPosition&) const override { return math::Vec2(0,0); }
    const EditBoxStyle& get_editbox_style() const override { return style_; }
    void set_editbox_style(const EditBoxStyle& s) override { style_=s; }
    void set_editbox_event_handler(IEditBoxEventHandler* h) override { handler_=h; }
};

// Factory functions
IGuiLabel* create_label_widget() { return new GuiLabel(); }
IGuiTextInput* create_text_input_widget() { return new GuiTextInput(); }
IGuiEditBox* create_editbox_widget() { return new GuiEditBox(); }

} // namespace gui
} // namespace window
