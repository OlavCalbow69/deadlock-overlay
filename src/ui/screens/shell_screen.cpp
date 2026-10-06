#include "application/brand.h"
#include "assets/avatars.h"
#include "ui/controls/scroll.h"
#include "ui/controls/theme_toggle.h"
#include "ui/controls/widgets.h"
#include "ui/foundation/draw.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/page_renderer.h"
#include "ui/screens/search_overlay.h"
#include "ui/screens/shell.h"
#include "ui/screens/shell_menus.h"

namespace solace
{

namespace
{

constexpr float k_width_expanded = 256.f;
constexpr float k_width_icon = 68.f;

constexpr float k_footer_h = 68.f;

constexpr float k_content_pad_x = 8.f;
constexpr float k_group_pad_x = 4.f;
constexpr float k_group_pad_y = 6.f;
constexpr float k_group_gap = 8.f;

constexpr float k_item_h = 36.f;
constexpr float k_item_gap = 2.f;
constexpr float k_item_round = 12.f;
constexpr float k_item_pad_x = 12.f;
constexpr float k_item_gap_x = 10.f;
constexpr float k_icon_slot = 20.f;
constexpr float k_icon = 16.f;

constexpr float k_label_h = 28.f;
constexpr float k_label_mb = 4.f;
constexpr float k_label_size = 10.f;
constexpr float k_label_track = 1.4f;

constexpr float k_sub_indent = 20.f;
constexpr float k_sub_pad_l = 12.f;
constexpr float k_sub_mt = 4.f;
constexpr float k_sub_item_h = 32.f;
constexpr float k_sub_item_gap = 2.f;
constexpr float k_sub_round = 8.f;

constexpr mo::spring_cfg k_morph{380.f, 35.f, 0.75f};

constexpr float k_label_enter = 0.2f, k_label_enter_delay = 0.08f;
constexpr float k_label_exit = 0.12f;

constexpr float k_sub_open = 0.2f, k_sub_open_delay = 0.035f, k_sub_open_stagger = 0.045f;
constexpr float k_sub_close = 0.14f, k_sub_close_stagger = 0.025f;
constexpr float k_sub_item = 0.18f;
constexpr float k_sub_blur = 3.f;

struct nav_item
{
    const char* label;
    icons::id icon;
    const char* badge;
    const char* sub[3];
    int sub_count;
};

const nav_item k_items[] = {
    {"Search", icons::id::search, nullptr, {nullptr, nullptr, nullptr}, 0},
    {"Assistant", icons::id::sparkles, nullptr, {nullptr, nullptr, nullptr}, 0},
    {"Messages", icons::id::inbox, "4", {nullptr, nullptr, nullptr}, 0},
    {"Settings",
     icons::id::circle_user_round,
     nullptr,
     {"All settings", "Recent changes", "Categories"},
     3},
    {"Presets", icons::id::building_2, nullptr, {nullptr, nullptr, nullptr}, 0},
    {"Patches", icons::id::target, nullptr, {"Pipeline", "Projection", "Live"}, 3},
    {"Tasks", icons::id::list_todo, nullptr, {nullptr, nullptr, nullptr}, 0},
    {"Notes", icons::id::notebook_tabs, nullptr, {nullptr, nullptr, nullptr}, 0},
    {"Automation", icons::id::workflow, nullptr, {"Rules", "Runs", "Presets"}, 3},
    {"Dashboard", icons::id::layout_grid, nullptr, {nullptr, nullptr, nullptr}, 0},
};

constexpr int k_item_count = IM_ARRAYSIZE(k_items);

static_assert(k_item_count == route_index(route::profile));

const char* page_title(route destination, int sub)
{
    if (destination == route::profile)
        return "Profile";
    if (destination == route::notifications)
        return "Notifications";
    if (destination == route::preferences)
        return "Preferences";

    const nav_item& item = k_items[ImClamp(route_index(destination), 0, k_item_count - 1)];
    return item.sub_count > 0 ? item.sub[ImClamp(sub, 0, item.sub_count - 1)] : item.label;
}

const search_item k_search[] = {
    {"Search", "Jump anywhere without leaving the keyboard", "command palette jump",
     icons::id::search, route::search, 0},
    {"Assistant", "Read a crash dump, tune your settings", "assistant ask reply",
     icons::id::sparkles, route::assistant, 0},
    {"Messages", "Four messages are waiting on you", "messages replies", icons::id::inbox,
     route::messages, 0},
    {"All settings", "Everything you can change, and its value", "settings options",
     icons::id::circle_user_round, route::settings, 0},
    {"Recent changes", "What changed, and when", "settings history", icons::id::circle_user_round,
     route::settings, 1},
    {"Categories", "Graphics, camera, display, audio", "settings groups",
     icons::id::circle_user_round, route::settings, 2},
    {"Presets", "Saved presets you can load or share", "presets configs", icons::id::building_2,
     route::presets, 0},
    {"Pipeline", "What is in test for the next patch", "patches pipeline", icons::id::target,
     route::patches, 0},
    {"Projection", "How stable the next patch looks", "patches forecast", icons::id::target,
     route::patches, 1},
    {"Live", "Shipped and pulled, with the reasons", "patches archive", icons::id::target,
     route::patches, 2},
    {"Tasks", "The short list, and what needs a nudge", "todo checklist", icons::id::list_todo,
     route::tasks, 0},
    {"Notes", "Longer-form thinking", "docs writing", icons::id::notebook_tabs, route::notes, 0},
    {"Rules", "Rules that run without being asked", "automation rules", icons::id::workflow,
     route::automation, 0},
    {"Runs", "Every run, and how it ended", "automation history", icons::id::workflow,
     route::automation, 1},
    {"Presets", "Starting points for a new keybind layout", "automation presets",
     icons::id::workflow, route::automation, 2},
    {"Dashboard", "Frame time, win rate, and hours played", "metrics reports",
     icons::id::layout_grid, route::dashboard, 0},
    {"Profile", "Your account and what it may send you", "account me settings",
     icons::id::circle_user_round, route::profile, 0},
};

constexpr int k_group1_count = 3;

constexpr ImU32 c_avatar = IM_COL32(0xD5, 0xFF, 0x66, 0xFF);

struct item_anim
{
    color_tween text;
    mo::spring press;
    mo::spring chevron;
    mo::presence sub;
    float sub_t = 0.f;
};

struct sidebar_state
{
    bool collapsed = false;
    mo::spring width;
    float label_t = 1e6f;
    bool labels_shown = true;

    route active = route::settings;
    int sub_index[route_count] = {};
    int force_open = -1;

    mo::spring profile_chevron;

    smooth_scroll rail;
    float rail_content = 0.f;

    mo::spring pill_x, pill_y, pill_w, pill_h;
    bool pill_seeded = false;

    item_anim items[k_item_count];
    color_tween trigger_col;
};

sidebar_state& state()
{
    static sidebar_state s;
    return s;
}

float submenu_height(const nav_item& item)
{
    if (item.sub_count <= 0)
        return 0.f;
    return k_sub_mt + (float)item.sub_count * k_sub_item_h +
           (float)(item.sub_count - 1) * k_sub_item_gap;
}

float submenu_reveal(const item_anim& a)
{
    if (!a.sub.mounted)
        return 0.f;
    if (a.sub.exiting)
        return 1.f - mo::EASE_OUT(ImClamp(a.sub.out / k_sub_close, 0.f, 1.f));
    return mo::EASE_OUT(ImClamp(a.sub.in / k_sub_open, 0.f, 1.f));
}

void draw_bell(ImDrawList* dl, const ImRect& rect, float alpha)
{
    const bool hot = notifications_trigger(rect);

    if (hot || notifications_open())
        dl->AddRectFilled(rect.Min, rect.Max, mo::with_alpha(c_card, alpha), px(10.f));

    const float box = px(16.f);
    icons::draw(
        icons::id::bell, dl,
        ImVec2(rect.GetCenter().x - box * 0.5f, rect.GetCenter().y - box * 0.5f), box,
        mo::with_alpha(hot || notifications_open() ? c_foreground : c_muted_foreground, alpha));

    const int unread = notifications_unread();
    if (unread <= 0)
        return;

    const ImVec2 at(rect.GetCenter().x + px(5.f), rect.GetCenter().y - px(5.f));
    dl->AddCircleFilled(at, px(4.f), mo::with_alpha(c_background, alpha));
    dl->AddCircleFilled(at, px(2.5f), mo::with_alpha(c_primary, alpha));
}

void draw_avatar(ImDrawList* dl, const ImVec2& tl, float size, float alpha)
{
    if (avatars::draw(dl, avatars::me(), tl, size, alpha))
        return;

    const ImVec2 br(tl.x + size, tl.y + size);
    dl->AddRectFilled(tl, br, mo::with_alpha(c_avatar, alpha), size * 0.5f);

    ImFont* f = font_semibold(12.f);
    const float w = text_width(f, brand::user_initials);
    draw_text(dl, f, ImVec2(tl.x + (size - w) * 0.5f, tl.y + (size - f->LegacySize) * 0.5f),
              mo::with_alpha(c_background, alpha), brand::user_initials);
}
} // namespace

bool menu_screen(float alpha, const shell_extension* extension)
{
    bool sign_out = false;

    sidebar_state& s = state();
    if (extension && extension->active_page)
    {
        switch (*extension->active_page)
        {
        case shell_page::camera: s.active = route::assistant; break;
        case shell_page::health: s.active = route::settings; break;
        case shell_page::connection: s.active = route::patches; break;
        }
    }
    const float dt = ImGui::GetIO().DeltaTime;

    const ImVec2 window_size = shell::animate_size(px(shell::width, shell::height));
    ui_runtime::host_size = window_size;

    ImGui::SetNextWindowSize(window_size);
    ImGui::SetNextWindowPos(extension ? px(24.f, 24.f) : ImVec2(0, 0));

    ImGui::Begin("Shell", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
    {
        ui_runtime::apply_style();

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList* dl = window->DrawList;
        if (extension)
        {
            dl->PushClipRectFullScreen();
            for (int i = 18; i >= 1; --i)
            {
                const float spread = px(static_cast<float>(i));
                dl->AddRectFilled(window->Pos - ImVec2(spread, spread) + px(0.f, 4.f),
                                  window->Pos + window->Size + ImVec2(spread, spread) + px(0.f, 4.f),
                                  IM_COL32(0, 0, 0, 3), px(shell::rounding) + spread);
            }
            dl->PopClipRect();
        }
        const ImRect plate = shell::plate(extension ? extension->frame_opacity : 1.f);

        const bool shortcut = ImGui::IsKeyPressed(ImGuiKey_B, false) &&
                              (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);

        if (shortcut)
            s.collapsed = !s.collapsed;

        if (s.labels_shown == s.collapsed)
        {
            s.labels_shown = !s.collapsed;
            s.label_t = 0.f;
        }
        s.label_t += dt;

        const float width = s.width.to(s.collapsed ? k_width_icon : k_width_expanded, k_morph, dt);
        const bool collapsed = s.collapsed;

        const float label_p =
            collapsed ? mo::EASE_OUT(ImClamp(s.label_t / k_label_exit, 0.f, 1.f))
                      : mo::EASE_OUT(
                            ImClamp((s.label_t - k_label_enter_delay) / k_label_enter, 0.f, 1.f));
        const float label_a = (collapsed ? 1.f - label_p : label_p) * alpha;
        const float label_dx = collapsed ? -4.f * label_p : -4.f * (1.f - label_p);

        const ImVec2 origin = plate.Min;
        const float bar_w = px(width);

        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);

        const ImRect nav_box(ImVec2(plate.Min.x + px(1.f), plate.Min.y + px(68.f)),
                             ImVec2(plate.Min.x + px(width), plate.Max.y - px(1.f + k_footer_h)));
        const float nav_scroll = scroll_area(
            s.rail, nav_box, s.rail_content > 0.f ? s.rail_content : nav_box.GetHeight());

        dl->AddRectFilled(ImVec2(origin.x + bar_w, origin.y + px(1.f)),
                          ImVec2(origin.x + bar_w + px(1.f), plate.Max.y - px(1.f)),
                          mo::with_alpha(c_border, alpha));

        {

            const ImRect chip(ImVec2(origin.x + px(12.f), origin.y + px(12.f)),
                              ImVec2(origin.x + bar_w - px(12.f), origin.y + px(56.f)));
            const bool chip_hot = !extension && target_trigger(chip);

            if (chip_hot || target_menu_open())
                dl->AddRectFilled(chip.Min, chip.Max, mo::with_alpha(c_card, alpha),
                                  px(k_item_round));

            const ImVec2 tile(origin.x + px(20.f), origin.y + px(20.f));
            const float tile_size = px(28.f);

            if (extension || !avatars::draw(dl, avatars::logo(target_index()), tile, tile_size, alpha, px(8.f)))
            {
                dl->AddRectFilled(tile, ImVec2(tile.x + tile_size, tile.y + tile_size),
                                  mo::with_alpha(c_foreground, alpha), px(8.f));
                icons::draw(icons::id::command, dl, ImVec2(tile.x + px(6.f), tile.y + px(6.f)),
                            px(k_icon), mo::with_alpha(c_background, alpha));
            }

            if (label_a > 0.004f)
            {
                const char* name = extension ? "Deadlock" : target_name();
                ImFont* f = font_semibold(text_sm);
                const ImVec2 at(origin.x + px(60.f + label_dx),
                                origin.y + px(24.f) + line_top(f, px(leading_sm)));
                draw_text(dl, f, at, mo::with_alpha(c_foreground, label_a), name);

                if (!extension) icons::draw(icons::id::chevrons_up_down, dl,
                            ImVec2(at.x + text_width(f, name) + px(8.f), origin.y + px(27.f)),
                            px(14.f),
                            mo::with_alpha(chip_hot ? c_foreground : c_muted_foreground, label_a));
            }
        }

        const float menu_x = origin.x + px(k_content_pad_x + k_group_pad_x);
        const float menu_w = bar_w - px((k_content_pad_x + k_group_pad_x) * 2.f);

        float y = origin.y + px(68.f + k_group_pad_y) - nav_scroll;
        const float nav_top_y = y;
        ImRect active_rect;
        bool have_active = false;

        dl->PushClipRect(nav_box.Min, nav_box.Max, true);

        for (int i = 0; i < k_item_count; i++)
        {
            if (extension && i != route_index(route::assistant) && i != route_index(route::settings) && i != route_index(route::patches))
                continue;
            nav_item item = k_items[i];
            if (extension)
                item = {i == route_index(route::assistant) ? "Camera Focus" : i == route_index(route::settings) ? "Health bars" : "Connection",
                        i == route_index(route::assistant) ? icons::id::crosshair_simple : i == route_index(route::settings) ? icons::id::eye : icons::id::workflow,
                        nullptr, {nullptr, nullptr, nullptr}, 0};
            item_anim& anim = s.items[i];

            if (extension ? i == route_index(route::assistant) : i == k_group1_count)
            {
                y += px(6.f + k_group_gap + 4.f);

                if (label_a > 0.004f)
                {

                    ImFont* f = font_medium(k_label_size);
                    draw_text_tracked(
                        dl, f, ImVec2(menu_x + px(8.f + label_dx), y + line_top(f, px(16.f))),
                        mo::with_alpha(c_muted_foreground, label_a), "GAME", px(k_label_track));
                }
                y += px(k_label_h + k_label_mb);
            }

            const ImRect bb(ImVec2(menu_x, y), ImVec2(menu_x + menu_w, y + px(k_item_h)));

            const bool row_visible = bb.Max.y > nav_box.Min.y && bb.Min.y < nav_box.Max.y;

            ImGui::PushID(i);
            const ImGuiID id = window->GetID("nav");
            ImGui::SetCursorScreenPos(bb.Min);
            ImGui::ItemSize(bb.GetSize());
            if (row_visible)
                ImGui::ItemAdd(bb, id);

            bool hovered = false, held = false;
            const bool pressed =
                row_visible && !pointer_claimed() && ImGui::ButtonBehavior(bb, id, &hovered, &held);
            ImGui::PopID();

            if (pressed)
            {
                s.active = route_from_index(i);
                if (extension && extension->active_page)
                    *extension->active_page = i == route_index(route::assistant) ? shell_page::camera : i == route_index(route::settings) ? shell_page::health : shell_page::connection;
                if (item.sub_count > 0)
                {
                    const bool open = anim.sub.mounted && !anim.sub.exiting;
                    if (!open && collapsed)
                        s.collapsed = false;
                    anim.sub_t = 0.f;
                }
            }

            const bool want_sub = item.sub_count > 0 && !collapsed &&
                                  (s.force_open == i ? true
                                   : pressed         ? !(anim.sub.mounted && !anim.sub.exiting)
                                                     : (anim.sub.mounted && !anim.sub.exiting));
            anim.sub.update(want_sub, dt,
                            k_sub_close + (float)item.sub_count * k_sub_close_stagger);
            anim.sub_t += dt;

            const bool is_active = (route_index(s.active) == i);
            const float scale = anim.press.to(held ? 0.98f : 1.f, mo::SPRING_PRESS, dt);

            if (is_active)
            {
                active_rect = bb;
                have_active = true;
            }

            const ImU32 text_col = anim.text.update(
                (is_active || hovered) ? c_foreground : c_muted_foreground, dt, 0.15f);

            const ImVec2 centre = bb.GetCenter();
            const ImVec2 half(bb.GetWidth() * 0.5f * scale, bb.GetHeight() * 0.5f * scale);
            const ImRect row(ImVec2(centre.x - half.x, centre.y - half.y),
                             ImVec2(centre.x + half.x, centre.y + half.y));

            const ImVec2 icon_slot(row.Min.x + px(k_item_pad_x), centre.y - px(k_icon_slot) * 0.5f);
            icons::draw(item.icon, dl,
                        ImVec2(icon_slot.x + px((k_icon_slot - k_icon) * 0.5f),
                               icon_slot.y + px((k_icon_slot - k_icon) * 0.5f)),
                        px(k_icon), mo::with_alpha(text_col, alpha));

            if (label_a > 0.004f)
            {
                ImFont* f = extension ? font_semibold(16) : font_medium(text_sm);
                draw_text(dl, f,
                          ImVec2(icon_slot.x + px(k_icon_slot + k_item_gap_x + label_dx),
                                 centre.y - f->LegacySize * 0.5f),
                          mo::with_alpha(text_col, label_a), item.label);

                if (item.badge)
                {
                    ImFont* bf = font_medium(text_xs);
                    const float bw = text_width(bf, item.badge);
                    draw_text(
                        dl, bf,
                        ImVec2(row.Max.x - px(k_item_pad_x) - bw, centre.y - bf->LegacySize * 0.5f),
                        mo::with_alpha(c_muted_foreground, label_a), item.badge);
                }
            }

            if (item.sub_count > 0)
            {

                const bool open = anim.sub.mounted && !anim.sub.exiting;
                const float rot =
                    anim.chevron.to(open ? 1.f : 0.f, mo::SPRING_LAYOUT, dt) * IM_PI * 0.5f;
                const float box = px(14.f);
                const ImVec2 c(row.Max.x - px(k_item_pad_x) - box * 0.5f + px(label_dx * -1.f),
                               centre.y);

                if (label_a > 0.004f)
                {
                    const int rotation_start = draw_utils::rotation_start(dl);
                    icons::draw(icons::id::chevron_right, dl,
                                ImVec2(c.x - box * 0.5f, c.y - box * 0.5f), box,
                                mo::with_alpha(c_muted_foreground, label_a));
                    draw_utils::rotate_vertices(dl, rotation_start, rot, c);
                }
            }

            y += px(k_item_h + k_item_gap);

            if (anim.sub.mounted && item.sub_count > 0)
            {
                const float reveal = submenu_reveal(anim);
                const float block_h = px(submenu_height(item));
                const float top = y - px(k_item_gap) + px(k_sub_mt);

                const float rail_x = menu_x + px(k_sub_indent);
                const ImRect clip(ImVec2(menu_x, top),
                                  ImVec2(menu_x + menu_w, top + (block_h - px(k_sub_mt)) * reveal));

                dl->PushClipRect(clip.Min, clip.Max, true);

                dl->AddRectFilled(ImVec2(rail_x, top),
                                  ImVec2(rail_x + px(1.f), top + block_h - px(k_sub_mt)),
                                  mo::with_alpha(c_border, alpha));

                for (int k = 0; k < item.sub_count; k++)
                {
                    const bool exiting = anim.sub.exiting;
                    const float delay = exiting
                                            ? (float)(item.sub_count - 1 - k) * k_sub_close_stagger
                                            : k_sub_open_delay + (float)k * k_sub_open_stagger;
                    const float t = exiting ? anim.sub.out : anim.sub.in;
                    const float p = mo::EASE_OUT(ImClamp((t - delay) / k_sub_item, 0.f, 1.f));

                    const float op = exiting ? 1.f - p : p;
                    const float dy = exiting ? -6.f * p : -6.f * (1.f - p);
                    const float blur = exiting ? k_sub_blur * p : k_sub_blur * (1.f - p);

                    const float sy = top + (float)k * px(k_sub_item_h + k_sub_item_gap) + px(dy);
                    const ImRect sb(ImVec2(rail_x + px(1.f + k_sub_pad_l), sy),
                                    ImVec2(menu_x + menu_w, sy + px(k_sub_item_h)));

                    ImGui::PushID(i * 16 + k + 1000);
                    const ImGuiID sid = window->GetID("sub");
                    ImGui::SetCursorScreenPos(sb.Min);
                    ImGui::ItemSize(ImVec2(0, 0));
                    ImGui::ItemAdd(sb, sid);
                    bool sh = false, shd = false;
                    if (!pointer_claimed())
                        ImGui::ButtonBehavior(sb, sid, &sh, &shd);
                    ImGui::PopID();

                    const bool sub_active = (route_index(s.active) == i && s.sub_index[i] == k);
                    if (shd && op > 0.5f)
                        s.sub_index[i] = k;

                    if ((sh || sub_active) && op > 0.5f)
                        dl->AddRectFilled(
                            sb.Min, sb.Max,
                            mo::with_alpha(c_card, (sub_active ? 0.7f : 0.6f) * op * alpha),
                            px(k_sub_round));

                    const ImU32 sub_col = mo::with_alpha(
                        (sh || sub_active) ? c_foreground : c_muted_foreground, op * alpha);
                    dl->AddCircleFilled(ImVec2(sb.Min.x + px(8.f + 2.f), sb.GetCenter().y), px(2.f),
                                        sub_col, 12);

                    ImFont* f = font_regular(text_xs);
                    if (blur > 0.25f)
                        draw_text_blur(dl, f,
                                       ImVec2(sb.Min.x + px(8.f + 16.f + 8.f),
                                              sb.GetCenter().y - f->LegacySize * 0.5f),
                                       sub_col, item.sub[k], px(blur));
                    else
                        draw_text(dl, f,
                                  ImVec2(sb.Min.x + px(8.f + 16.f + 8.f),
                                         sb.GetCenter().y - f->LegacySize * 0.5f),
                                  sub_col, item.sub[k]);
                }

                dl->PopClipRect();
                y += block_h;
            }
        }

        s.rail_content = (y - nav_top_y) + px(k_group_pad_y);
        scrollbar(dl, nav_box, s.rail_content, nav_scroll, alpha);
        dl->PopClipRect();

        splitter.SetCurrentChannel(dl, 0);
        dl->PushClipRect(nav_box.Min, nav_box.Max, true);
        if (have_active)
        {
            if (!s.pill_seeded)
            {
                s.pill_x.snap(active_rect.Min.x);
                s.pill_y.snap(active_rect.Min.y);
                s.pill_w.snap(active_rect.GetWidth());
                s.pill_h.snap(active_rect.GetHeight());
                s.pill_seeded = true;
            }

            const float rx = s.pill_x.to(active_rect.Min.x, mo::SPRING_LAYOUT, dt);
            const float ry = s.pill_y.to(active_rect.Min.y, mo::SPRING_LAYOUT, dt);
            const float rw = s.pill_w.to(active_rect.GetWidth(), mo::SPRING_LAYOUT, dt);
            const float rh = s.pill_h.to(active_rect.GetHeight(), mo::SPRING_LAYOUT, dt);

            dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + rw, ry + rh),
                              mo::with_alpha(c_card, alpha), px(k_item_round));
        }
        dl->PopClipRect();
        splitter.Merge(dl);

        if (extension)
        {
            const float top = plate.Max.y - px(1.f + k_footer_h);
            dl->AddRectFilled(ImVec2(origin.x + px(1.f), top), ImVec2(origin.x + bar_w, top + px(1.f)),
                              mo::with_alpha(c_border, alpha));
            icons::draw(icons::id::target, dl, ImVec2(origin.x + px(25.f), top + px(25.f)),
                        px(18.f), mo::with_alpha(c_success, alpha));
            if (label_a > 0.004f)
            {
                draw_text(dl, font_medium(text_sm), ImVec2(origin.x + px(60.f), top + px(17.f)),
                          mo::with_alpha(c_foreground, label_a), "External overlay");
                draw_text(dl, font_regular(text_xs), ImVec2(origin.x + px(60.f), top + px(38.f)),
                          mo::with_alpha(c_muted_foreground, label_a), "Insert to open / close");
            }
        }
        else
        {
            const float top = plate.Max.y - px(1.f + k_footer_h);
            dl->AddRectFilled(ImVec2(origin.x + px(1.f), top),
                              ImVec2(origin.x + bar_w, top + px(1.f)),
                              mo::with_alpha(c_border, alpha));

            const ImRect row(ImVec2(origin.x + px(9.f), top + px(10.f)),
                             ImVec2(origin.x + bar_w - px(9.f), top + px(58.f)));
            const bool row_hot = profile_trigger(row);

            if (row_hot || profile_menu_open())
                dl->AddRectFilled(row.Min, row.Max, mo::with_alpha(c_card, alpha),
                                  px(k_item_round));

            const ImVec2 av(origin.x + px(17.f), top + px(16.f));
            draw_avatar(dl, av, px(36.f), alpha);

            if (label_a > 0.004f)
            {
                ImFont* nf = font_medium(text_sm);
                draw_text(dl, nf,
                          ImVec2(origin.x + px(65.f + label_dx),
                                 top + px(17.f) + line_top(nf, px(leading_sm))),
                          mo::with_alpha(c_foreground, label_a), brand::user_name);

                ImFont* ef = font_regular(text_xs);
                draw_text(dl, ef,
                          ImVec2(origin.x + px(65.f + label_dx),
                                 top + px(37.f) + line_top(ef, px(leading_xs))),
                          mo::with_alpha(c_muted_foreground, label_a), brand::user_github);

                const float turn =
                    s.profile_chevron.to(profile_menu_open() ? 1.f : 0.f, mo::SPRING_LAYOUT, dt);
                const ImVec2 at(origin.x + px(224.f), top + px(27.f));
                const ImVec2 centre(at.x + px(k_icon) * 0.5f, at.y + px(k_icon) * 0.5f);

                const int rotation_start = draw_utils::rotation_start(dl);
                icons::draw(icons::id::chevron_right, dl, at, px(k_icon),
                            mo::with_alpha(row_hot ? c_foreground : c_muted_foreground, label_a));
                draw_utils::rotate_vertices(dl, rotation_start, -turn * IM_PI * 0.5f, centre);
            }
        }

        {
            const float ix = origin.x + bar_w + px(1.f);
            ImFont* f14 = extension ? font_semibold(16) : font_medium(text_sm);

            const ImRect trig(ImVec2(ix + px(16.f), origin.y + px(13.f)),
                              ImVec2(ix + px(56.f), origin.y + px(53.f)));

            ImGui::PushID("trigger");
            const ImGuiID tid = window->GetID("t");
            ImGui::SetCursorScreenPos(trig.Min);
            ImGui::ItemSize(ImVec2(0, 0));
            ImGui::ItemAdd(trig, tid);
            bool th = false, thd = false;
            if (ImGui::ButtonBehavior(trig, tid, &th, &thd))
                s.collapsed = !s.collapsed;
            ImGui::PopID();

            const ImU32 tcol =
                s.trigger_col.update(th ? c_foreground : c_muted_foreground, dt, 0.15f);
            if (th)
                dl->AddRectFilled(trig.Min, trig.Max, mo::with_alpha(c_card, alpha),
                                  px(k_item_round));
            icons::draw(icons::id::panel_left, dl,
                        ImVec2(trig.GetCenter().x - px(k_icon) * 0.5f,
                               trig.GetCenter().y - px(k_icon) * 0.5f),
                        px(k_icon), mo::with_alpha(tcol, alpha));

            dl->AddRectFilled(ImVec2(ix + px(64.f), origin.y + px(23.f)),
                              ImVec2(ix + px(65.f), origin.y + px(43.f)),
                              mo::with_alpha(c_border, alpha));

            const int active_index = route_index(s.active);
            const char* crumb_text = extension ? (s.active == route::assistant ? "Camera Focus" : s.active == route::patches ? "Connection" : "Health bars")
                                              : page_title(s.active, s.sub_index[active_index]);
            draw_text(dl, f14,
                      ImVec2(ix + px(81.f), origin.y + px(22.f) + line_top(f14, px(leading_sm))),
                      mo::with_alpha(c_foreground, alpha), crumb_text);

            const float bar_right = plate.Max.x - px(16.f);
            const float button = px(40.f);
            const float gap = px(sp_2);

            const ImRect toggle_rect(ImVec2(bar_right - button, origin.y + px(12.f)),
                                     ImVec2(bar_right, origin.y + px(52.f)));
            theme_toggle("theme", toggle_rect, 16.f, alpha);

            const ImRect bell_rect(ImVec2(toggle_rect.Min.x - gap - button, origin.y + px(12.f)),
                                   ImVec2(toggle_rect.Min.x - gap, origin.y + px(52.f)));
            if (extension)
            {
                ImGui::SetCursorScreenPos(bell_rect.Min);
                if (ImGui::InvisibleButton("close_overlay_menu", bell_rect.GetSize()) && extension->close)
                    extension->close(extension->context);
                const bool hovered = ImGui::IsItemHovered();
                if (hovered) dl->AddRectFilled(bell_rect.Min, bell_rect.Max, c_card, px(12.f));
                icons::draw(icons::id::cross, dl, bell_rect.Min + px(12.f, 12.f), px(16.f),
                            mo::with_alpha(hovered ? c_foreground : c_muted_foreground, alpha));
                if (extension->appearance)
                {
                    const ImRect appearance_rect(bell_rect.Min - px(48.f, 0.f), bell_rect.Max - px(48.f, 0.f));
                    ImGui::SetCursorScreenPos(appearance_rect.Min);
                    if (ImGui::InvisibleButton("menu_appearance", appearance_rect.GetSize()))
                        ImGui::OpenPopup("Menu appearance");
                    const bool appearance_hot = ImGui::IsItemHovered();
                    if (appearance_hot)
                    {
                        dl->AddRectFilled(appearance_rect.Min, appearance_rect.Max, c_card, px(12.f));
                        ImGui::SetTooltip("Menu appearance");
                    }
                    icons::draw(icons::id::sparkles, dl, appearance_rect.Min + px(12.f, 12.f), px(16.f),
                                mo::with_alpha(appearance_hot ? c_foreground : c_muted_foreground, alpha));
                    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(12.f));
                    if (ImGui::BeginPopup("Menu appearance"))
                    {
                        extension->appearance(extension->context);
                        ImGui::EndPopup();
                    }
                    ImGui::PopStyleVar();
                }
            }
            else draw_bell(dl, bell_rect, alpha);

            const float crumb_end = ix + px(81.f) + text_width(f14, crumb_text) + px(24.f);
            const float search_right = bell_rect.Min.x - gap;
            const float search_left = ImMax(crumb_end, search_right - px(search_trigger_w));

            if (!extension && search_right - search_left >= px(160.f))
            {
                const ImRect search_rect(
                    ImVec2(search_left, origin.y + px(8.f)),
                    ImVec2(search_right, origin.y + px(8.f) + px(search_trigger_h)));
                morphing_search_trigger(search_rect, alpha);
            }

            dl->AddRectFilled(ImVec2(ix, origin.y + px(64.f)),
                              ImVec2(plate.Max.x - px(1.f), origin.y + px(65.f)),
                              mo::with_alpha(c_border, alpha));

            const float bx = ix + px(28.f);
            const bool on_profile = is_account_route(s.active);
            const nav_item& current = k_items[on_profile ? 0 : active_index];
            const ImRect body(ImVec2(bx, origin.y + px(88.f)),
                              ImVec2(plate.Max.x - px(29.f), origin.y + px(628.f)));

            if (extension && extension->content)
                extension->content(s.active == route::assistant ? shell_page::camera : s.active == route::patches ? shell_page::connection : shell_page::health, body, alpha, extension->context);
            else draw_page(s.active, page_title(s.active, s.sub_index[active_index]),
                      on_profile ? nullptr : current.sub, on_profile ? 0 : current.sub_count,
                      &s.sub_index[active_index], body, alpha);

            ImFont* f12 = font_medium(text_xs);
            const float rule_y = plate.Max.y - px(1.f + k_footer_h);
            dl->AddRectFilled(ImVec2(ix, rule_y), ImVec2(plate.Max.x - px(1.f), rule_y + px(1.f)),
                              mo::with_alpha(c_border, alpha));

            ImFont* f10 = font_regular(k_label_size);
            draw_text_tracked(dl, f10, ImVec2(bx, rule_y + px(12.f) + line_top(f10, px(15.f))),
                              mo::with_alpha(c_muted_foreground, alpha), "ACTIVE VIEW", px(1.6f));

            draw_text(dl, f14, ImVec2(bx, rule_y + px(31.f) + line_top(f14, px(leading_sm))),
                      mo::with_alpha(c_foreground, alpha), extension ? extension->status : crumb_text);

            const char* hint = extension ? "Insert  /  drag to move" : "Press Ctrl+B to toggle";
            const float hw = text_width(f12, hint);
            draw_text(dl, f12,
                      ImVec2(plate.Max.x - px(29.f) - hw,
                             rule_y + px(35.f) + line_top(f12, px(leading_xs))),
                      mo::with_alpha(c_muted_foreground, alpha), hint);
        }

        if (!extension)
        {
            const int hit = morphing_search_overlay(plate, k_search, IM_ARRAYSIZE(k_search), alpha);
            if (hit >= 0)
            {
                s.active = k_search[hit].destination;
                const int destination_index = route_index(s.active);
                s.sub_index[destination_index] = k_search[hit].sub;
                s.force_open =
                    destination_index < k_item_count && k_items[destination_index].sub_count > 0
                        ? destination_index
                        : -1;
            }
            else
            {
                s.force_open = -1;
            }
        }

        if (!extension && target_menu(plate, alpha))
            sign_out = true;
        switch (extension ? profile_none : profile_menu(plate, alpha))
        {
        case profile_sign_out:
            sign_out = true;
            break;
        case profile_open_page:
            s.active = route::profile;
            break;
        case profile_open_notifications:
            s.active = route::notifications;
            break;
        case profile_open_preferences:
            s.active = route::preferences;
            break;
        default:
            break;
        }
        if (!extension) notifications_panel(plate, alpha);

        flush_overlays();
        toasts_draw(plate);
    }
    ImGui::End();
    return sign_out;
}
} // namespace solace
