#pragma once

#include "imgui.h"

#include <memory>
#include <unordered_map>

namespace solace::ui_runtime
{
inline float scale = 1.f;
inline bool fonts_dirty = false;
inline ImVec2 host_size{380.f, 300.f};

inline void set_scale(float value, bool rebuild_fonts)
{
    scale = value > 0.f ? value : 1.f;
    fonts_dirty = fonts_dirty || rebuild_fonts;
}

namespace detail
{
class animation_bucket_base
{
  public:
    virtual ~animation_bucket_base() = default;
    virtual void collect(int current_frame, int max_unused_frames) = 0;
    virtual void clear() noexcept = 0;
};

void register_animation_bucket(animation_bucket_base* bucket);

template <typename T> class animation_bucket final : public animation_bucket_base
{
  public:
    animation_bucket()
    {
        register_animation_bucket(this);
    }

    T* get(ImGuiID id, int current_frame)
    {
        entry& item = states_[id];
        if (!item.value)
            item.value = std::make_unique<T>();
        item.last_seen_frame = current_frame;
        return item.value.get();
    }

    void collect(int current_frame, int max_unused_frames) override
    {
        for (auto item = states_.begin(); item != states_.end();)
        {
            if (current_frame - item->second.last_seen_frame > max_unused_frames)
                item = states_.erase(item);
            else
                ++item;
        }
    }

    void clear() noexcept override
    {
        states_.clear();
    }

  private:
    struct entry
    {
        std::unique_ptr<T> value;
        int last_seen_frame = 0;
    };

    std::unordered_map<ImGuiID, entry> states_;
};
} // namespace detail

template <typename T> T* animation_state(ImGuiID id)
{
    static detail::animation_bucket<T> states;
    const ImGuiContext* context = ImGui::GetCurrentContext();
    return states.get(id, context ? ImGui::GetFrameCount() : 0);
}

void collect_animation_states(int max_unused_frames = 3600);
void clear_animation_states() noexcept;

inline void push_font(ImFont* font)
{
    ImGui::PushFont(font, font ? font->LegacySize : 0.f);
}

inline void pop_font()
{
    ImGui::PopFont();
}

void apply_style();
} // namespace solace::ui_runtime
