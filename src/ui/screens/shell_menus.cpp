#include "ui/screens/shell_menus.h"

#include "application/brand.h"
#include "assets/avatars.h"
#include "ui/controls/widgets.h"
#include "ui/foundation/motion/motion.h"
#include "ui/foundation/primitives.h"

namespace solace
{
namespace
{

constexpr float k_open = 0.18f;
constexpr float k_stagger = 0.035f;
constexpr float k_delay = 0.05f;
constexpr float k_item_blur = 3.f;

constexpr float k_panel_w = 232.f;
constexpr float k_pad = 4.f;
constexpr float k_row = 34.f;
constexpr float k_target_row = 40.f;
constexpr float k_account_h = 56.f;
constexpr float k_rule = 9.f;
constexpr float k_label_h = 24.f;

constexpr ImU32 c_avatar = IM_COL32(0xD5, 0xFF, 0x66, 0xFF);

struct target_entry
{
    const char* name;
    const char* version;
};

const target_entry k_targets[] = {
    {brand::game, "1.4.2"},
    {"Solace Beta", "0.9.7"},
    {"Solace Gamma", "2.1.0"},
};

struct action
{
    const char* label;
    icons::id icon;
    const char* badge;
    bool destructive;
};

const action k_target_actions[] = {
    {"Add a game", icons::id::plus, nullptr, false},
    {"Game settings", icons::id::settings, nullptr, false},
    {"Invite a friend", icons::id::user_plus, nullptr, false},
    {"Sign out", icons::id::log_out, nullptr, true},
};

const action k_profile_actions[] = {
    {"Profile", icons::id::user, nullptr, false},
    {"Notifications", icons::id::bell, "4", false},
    {"Preferences", icons::id::settings, nullptr, false},
    {"Sign out", icons::id::log_out, nullptr, true},
};

struct notice
{
    const char* title;
    const char* detail;
    const char* when;
    icons::id icon;
    bool unread;
};

const notice k_notices[] = {
    {"1.5 Seasons moved to Playtest", "Corvid cut the build", "12m ago", icons::id::target, true},
    {"Three tasks were assigned", "Kestrel, from the playtest", "1h ago", icons::id::list_todo,
     true},
    {"Photo mode passed review", "No regressions on a clean build", "2h ago", icons::id::check,
     true},
    {"Weekly digest is ready", "Monday 09:00, local time", "Yesterday", icons::id::workflow, false},
    {"Solace Lab joined the playtest", "Invited by you", "Monday", icons::id::circle_user_round,
     false},
};

constexpr int k_notice_count = IM_ARRAYSIZE(k_notices);
constexpr float k_notice_row = 56.f;

constexpr int k_target_count = IM_ARRAYSIZE(k_targets);
constexpr int k_target_action_count = IM_ARRAYSIZE(k_target_actions);
constexpr int k_profile_action_count = IM_ARRAYSIZE(k_profile_actions);

struct panel_state
{
    bool open = false;
    mo::presence panel;
    ImRect trigger;
    bool have_trigger = false;
};

struct menu_state
{
    panel_state target;
    panel_state profile;
    panel_state notifications;
    int current_target = 0;
    bool notice_read[k_notice_count] = {};
};

menu_state& state()
{
    static menu_state s;
    return s;
}

struct row_anim
{
    float opacity, dy, blur;
};

row_anim row_at(const panel_state& s, int index, bool exiting, float open)
{
    const float t = exiting ? s.panel.out : s.panel.in;
    const float delay = k_delay + (float)index * k_stagger;
    const float p = mo::EASE_OUT(ImClamp((t - delay) / k_open, 0.f, 1.f));

    row_anim out;
    out.opacity = (exiting ? 1.f - p : p) * open;
    out.dy = px(-6.f) * (exiting ? p : 1.f - p);
    out.blur = px(k_item_blur) * (exiting ? p : 1.f - p);
    return out;
}

void row_text(ImDrawList* dl, ImFont* f, const ImVec2& at, ImU32 col, const char* s, float blur)
{
    if (blur > 0.25f)
        draw_text_blur(dl, f, at, col, s, blur);
    else
        draw_text(dl, f, at, col, s);
}

void panel_surface(ImDrawList* dl, const ImRect& panel, float open)
{
    backdrop_blur(dl, panel, px(24.f), px(12.f), open);
    dl->AddRectFilled(panel.Min, panel.Max, mo::with_alpha(c_card, 0.95f * open), px(12.f));
    dl->AddRect(ImVec2(panel.Min.x + px(0.5f), panel.Min.y + px(0.5f)),
                ImVec2(panel.Max.x - px(0.5f), panel.Max.y - px(0.5f)),
                mo::with_alpha(c_border, open), px(12.f), px(1.f), ImDrawFlags_None);
}

void rule_at(ImDrawList* dl, const ImRect& panel, float y, float opacity)
{
    dl->AddRectFilled(ImVec2(panel.Min.x + px(k_pad), y + px(4.f)),
                      ImVec2(panel.Max.x - px(k_pad), y + px(5.f)),
                      mo::with_alpha(c_border, opacity));
}

void account_block(ImDrawList* dl, const ImRect& panel, float y, const row_anim& a)
{
    const float avatar = px(32.f);
    const ImVec2 at(panel.Min.x + px(12.f), y + a.dy + (px(k_account_h) - avatar) * 0.5f);

    if (!avatars::draw(dl, avatars::me(), at, avatar, a.opacity))
    {
        dl->AddRectFilled(at, ImVec2(at.x + avatar, at.y + avatar),
                          mo::with_alpha(c_avatar, a.opacity), avatar * 0.5f);

        ImFont* af = font_semibold(text_xs);
        draw_text(dl, af,
                  ImVec2(at.x + avatar * 0.5f - text_width(af, brand::user_initials) * 0.5f,
                         at.y + avatar * 0.5f - af->LegacySize * 0.5f),
                  mo::with_alpha(c_background, a.opacity), brand::user_initials);
    }

    ImFont* nf = font_medium(text_sm);
    row_text(dl, nf,
             ImVec2(at.x + avatar + px(10.f), y + a.dy + px(12.f) + line_top(nf, px(leading_sm))),
             mo::with_alpha(c_foreground, a.opacity), brand::user_name, a.blur);

    ImFont* ef = font_regular(text_xs);
    row_text(dl, ef,
             ImVec2(at.x + avatar + px(10.f), y + a.dy + px(31.f) + line_top(ef, px(leading_xs))),
             mo::with_alpha(c_muted_foreground, a.opacity), brand::user_github, a.blur);
}

bool action_row(ImDrawList* dl, const ImRect& panel, float y, const row_anim& a, const action& item,
                bool exiting, const ImVec2& mouse)
{
    const ImRect r(ImVec2(panel.Min.x + px(k_pad), y + a.dy),
                   ImVec2(panel.Max.x - px(k_pad), y + a.dy + px(k_row)));

    const bool hot = !exiting && r.Contains(mouse);
    if (hot)
        dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_foreground, 0.06f * a.opacity), px(8.f));

    const ImU32 col = mo::with_alpha(
        item.destructive ? c_destructive : (hot ? c_foreground : c_muted_foreground), a.opacity);

    icons::draw(item.icon, dl, ImVec2(r.Min.x + px(10.f), r.GetCenter().y - px(8.f)), px(16.f),
                col);

    ImFont* f = font_medium(text_sm);
    row_text(dl, f, ImVec2(r.Min.x + px(36.f), r.GetCenter().y - f->LegacySize * 0.5f), col,
             item.label, a.blur);

    if (item.badge)
    {
        ImFont* bf = font_regular(text_xs);
        draw_text(dl, bf,
                  ImVec2(r.Max.x - px(12.f) - text_width(bf, item.badge),
                         r.GetCenter().y - bf->LegacySize * 0.5f),
                  mo::with_alpha(c_muted_foreground, a.opacity), item.badge);
    }

    return hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool trigger(panel_state& self, panel_state& other_a, panel_state& other_b, const char* id,
             const ImRect& rect)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    self.trigger = rect;
    self.have_trigger = true;

    ImGui::PushID(id);
    const ImGuiID item_id = window->GetID("chip");
    ImGui::SetCursorScreenPos(rect.Min);
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(rect, item_id);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(rect, item_id, &hovered, &held);
    ImGui::PopID();

    if (pressed)
    {
        self.open = !self.open;
        if (self.open)
        {
            other_a.open = false;
            other_b.open = false;
        }
    }

    return hovered;
}

bool begin_panel(panel_state& s, const ImRect& panel, int row_count, bool* out_exiting,
                 float* out_open)
{
    const float dt = ImGui::GetIO().DeltaTime;

    if (s.open && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        s.open = false;

    if (!s.panel.update(s.open, dt, k_open + k_delay + (float)row_count * k_stagger))
        return false;

    *out_exiting = s.panel.exiting;
    const float p =
        mo::EASE_OUT(ImClamp((*out_exiting ? s.panel.out : s.panel.in) / k_open, 0.f, 1.f));
    *out_open = (*out_exiting ? 1.f - p : p);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!*out_exiting && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !panel.Contains(mouse) &&
        !s.trigger.Contains(mouse))
        s.open = false;

    if (!*out_exiting)
        claim_pointer();

    return true;
}
} // namespace

bool target_menu_open()
{
    return state().target.open;
}
const char* target_name()
{
    return k_targets[state().current_target].name;
}
int target_index()
{
    return state().current_target;
}

bool target_trigger(const ImRect& rect)
{
    menu_state& m = state();
    return trigger(m.target, m.profile, m.notifications, "target", rect);
}

bool target_menu(const ImRect& viewport, float alpha)
{
    menu_state& m = state();
    panel_state& s = m.target;
    if (!s.have_trigger)
        return false;

    ImDrawList* dl = ImGui::GetCurrentWindow()->DrawList;

    const float w = px(k_panel_w);
    const float h = px(k_pad) * 2.f + px(k_account_h) + px(k_rule) + px(k_label_h) +
                    px(k_target_row) * (float)k_target_count + px(k_rule) +
                    px(k_row) * (float)(k_target_action_count - 1) + px(k_rule) + px(k_row);

    const float left = ImMin(s.trigger.Min.x, viewport.Max.x - px(16.f) - w);
    const float top = ImMin(s.trigger.Max.y + px(6.f), viewport.Max.y - px(16.f) - h);
    const ImRect panel(ImVec2(left, top), ImVec2(left + w, top + h));

    bool exiting = false;
    float open = 0.f;
    if (!begin_panel(s, panel, k_target_count + k_target_action_count, &exiting, &open))
        return false;
    open *= alpha;

    panel_surface(dl, panel, open);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    bool sign_out = false;
    int index = 0;
    float y = panel.Min.y + px(k_pad);

    account_block(dl, panel, y, row_at(s, index++, exiting, open));
    y += px(k_account_h);
    rule_at(dl, panel, y, open);
    y += px(k_rule);

    {
        ImFont* lf = font_regular(10.f);
        draw_text_tracked(dl, lf, ImVec2(panel.Min.x + px(16.f), y + line_top(lf, px(k_label_h))),
                          mo::with_alpha(c_muted_foreground, open), "GAMES", px(1.6f));
        y += px(k_label_h);
    }

    for (int i = 0; i < k_target_count; i++)
    {
        const row_anim a = row_at(s, index++, exiting, open);
        const ImRect r(ImVec2(panel.Min.x + px(k_pad), y + a.dy),
                       ImVec2(panel.Max.x - px(k_pad), y + a.dy + px(k_target_row)));

        const bool hot = !exiting && r.Contains(mouse);
        if (hot)
        {
            dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_foreground, 0.06f * a.opacity),
                              px(8.f));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                m.current_target = i;
                s.open = false;
                toast("Game switched", k_targets[i].name, toast_info);
            }
        }

        const float chip = px(24.f);
        const ImVec2 at(r.Min.x + px(8.f), r.GetCenter().y - chip * 0.5f);

        if (!avatars::draw(dl, avatars::logo(i), at, chip, a.opacity, px(7.f)))
        {
            dl->AddRectFilled(at, ImVec2(at.x + chip, at.y + chip),
                              mo::with_alpha(c_foreground, 0.08f * a.opacity), px(7.f));

            ImFont* cf = font_semibold(10.f);
            char initial[2] = {k_targets[i].name[0], 0};
            draw_text(dl, cf,
                      ImVec2(at.x + chip * 0.5f - text_width(cf, initial) * 0.5f,
                             at.y + chip * 0.5f - cf->LegacySize * 0.5f),
                      mo::with_alpha(c_foreground, a.opacity), initial);
        }

        ImFont* nf = font_medium(text_sm);
        row_text(dl, nf,
                 ImVec2(at.x + chip + px(10.f), r.Min.y + px(6.f) + line_top(nf, px(leading_sm))),
                 mo::with_alpha(i == m.current_target || hot ? c_foreground : c_muted_foreground,
                                a.opacity),
                 k_targets[i].name, a.blur);

        ImFont* mf = font_regular(text_xs);
        row_text(dl, mf,
                 ImVec2(at.x + chip + px(10.f), r.Min.y + px(22.f) + line_top(mf, px(leading_xs))),
                 mo::with_alpha(c_muted_foreground, 0.8f * a.opacity), k_targets[i].version,
                 a.blur);

        if (i == m.current_target)
            icons::draw(icons::id::check, dl,
                        ImVec2(r.Max.x - px(10.f) - px(14.f), r.GetCenter().y - px(7.f)), px(14.f),
                        mo::with_alpha(c_foreground, a.opacity));

        y += px(k_target_row);
    }

    rule_at(dl, panel, y, open);
    y += px(k_rule);

    for (int i = 0; i < k_target_action_count; i++)
    {

        if (k_target_actions[i].destructive)
        {
            rule_at(dl, panel, y, open);
            y += px(k_rule);
        }

        const row_anim a = row_at(s, index++, exiting, open);
        if (action_row(dl, panel, y, a, k_target_actions[i], exiting, mouse))
        {
            s.open = false;
            if (k_target_actions[i].destructive)
                sign_out = true;
            else if (i == 0)
                toast("Nothing to name yet", "Adding a game is stubbed in the demo");
            else
                toast(k_target_actions[i].label, k_targets[m.current_target].name, toast_info);
        }
        y += px(k_row);
    }

    return sign_out;
}

bool notifications_open()
{
    return state().notifications.open;
}

int notifications_unread()
{
    const menu_state& m = state();
    int n = 0;
    for (int i = 0; i < k_notice_count; i++)
        if (k_notices[i].unread && !m.notice_read[i])
            n++;
    return n;
}

bool notifications_trigger(const ImRect& rect)
{
    menu_state& m = state();
    return trigger(m.notifications, m.target, m.profile, "notifications", rect);
}

void notifications_panel(const ImRect& viewport, float alpha)
{
    menu_state& m = state();
    panel_state& s = m.notifications;
    if (!s.have_trigger)
        return;

    ImDrawList* dl = ImGui::GetCurrentWindow()->DrawList;

    const float w = px(320.f);
    const float h = px(k_pad) * 2.f + px(k_label_h) + px(k_notice_row) * (float)k_notice_count +
                    px(k_rule) + px(k_row);

    const float left =
        ImClamp(s.trigger.Max.x - w, viewport.Min.x + px(16.f), viewport.Max.x - px(16.f) - w);
    const float top = ImMin(s.trigger.Max.y + px(6.f), viewport.Max.y - px(16.f) - h);
    const ImRect panel(ImVec2(left, top), ImVec2(left + w, top + h));

    bool exiting = false;
    float open = 0.f;
    if (!begin_panel(s, panel, k_notice_count + 1, &exiting, &open))
        return;
    open *= alpha;

    panel_surface(dl, panel, open);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int index = 0;
    float y = panel.Min.y + px(k_pad);

    {
        ImFont* lf = font_regular(10.f);
        draw_text_tracked(dl, lf, ImVec2(panel.Min.x + px(16.f), y + line_top(lf, px(k_label_h))),
                          mo::with_alpha(c_muted_foreground, open), "NOTIFICATIONS", px(1.6f));

        const int unread = notifications_unread();
        if (unread > 0)
        {
            char count[16];
            ImFormatString(count, IM_ARRAYSIZE(count), "%d new", unread);
            ImFont* cf = font_medium(text_xs);
            draw_text(dl, cf,
                      ImVec2(panel.Max.x - px(16.f) - text_width(cf, count),
                             y + line_top(cf, px(k_label_h))),
                      mo::with_alpha(c_primary, open), count);
        }
        y += px(k_label_h);
    }

    for (int i = 0; i < k_notice_count; i++)
    {
        const row_anim a = row_at(s, index++, exiting, open);
        const ImRect r(ImVec2(panel.Min.x + px(k_pad), y + a.dy),
                       ImVec2(panel.Max.x - px(k_pad), y + a.dy + px(k_notice_row)));

        const bool hot = !exiting && r.Contains(mouse);
        if (hot)
        {
            dl->AddRectFilled(r.Min, r.Max, mo::with_alpha(c_foreground, 0.06f * a.opacity),
                              px(8.f));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                m.notice_read[i] = true;
        }

        const bool unread = k_notices[i].unread && !m.notice_read[i];

        const float chip = px(28.f);
        const ImVec2 at(r.Min.x + px(10.f), r.GetCenter().y - chip * 0.5f);
        dl->AddRectFilled(at, ImVec2(at.x + chip, at.y + chip),
                          mo::with_alpha(c_primary, (unread ? 0.10f : 0.05f) * a.opacity),
                          chip * 0.5f);
        icons::draw(k_notices[i].icon, dl, ImVec2(at.x + px(6.f), at.y + px(6.f)), px(16.f),
                    mo::with_alpha(unread ? c_primary : c_muted_foreground, a.opacity));

        const float tx = at.x + chip + px(10.f);
        const float text_w = r.Max.x - px(12.f) - px(52.f) - tx;

        dl->PushClipRect(ImVec2(tx, r.Min.y), ImVec2(ImMax(tx, tx + text_w), r.Max.y), true);

        ImFont* tf = unread ? font_medium(text_sm) : font_regular(text_sm);
        row_text(dl, tf, ImVec2(tx, r.Min.y + px(10.f) + line_top(tf, px(leading_sm))),
                 mo::with_alpha(c_foreground, a.opacity), k_notices[i].title, a.blur);

        ImFont* df = font_regular(text_xs);
        row_text(dl, df, ImVec2(tx, r.Min.y + px(30.f) + line_top(df, px(leading_xs))),
                 mo::with_alpha(c_muted_foreground, a.opacity), k_notices[i].detail, a.blur);
        dl->PopClipRect();

        ImFont* wf = font_regular(text_xs);
        draw_text(
            dl, wf,
            ImVec2(r.Max.x - px(12.f) - text_width(wf, k_notices[i].when), r.Min.y + px(12.f)),
            mo::with_alpha(c_muted_foreground, a.opacity), k_notices[i].when);

        if (unread)
            dl->AddCircleFilled(ImVec2(r.Max.x - px(16.f), r.Min.y + px(36.f)), px(3.f),
                                mo::with_alpha(c_primary, a.opacity));

        y += px(k_notice_row);
    }

    rule_at(dl, panel, y, open);
    y += px(k_rule);

    {
        const row_anim a = row_at(s, index++, exiting, open);
        const action mark{"Mark all as read", icons::id::check, nullptr, false};
        if (action_row(dl, panel, y, a, mark, exiting, mouse))
        {
            for (int i = 0; i < k_notice_count; i++)
                m.notice_read[i] = true;
            s.open = false;
            toast("Notifications cleared", "Nothing left to catch up on", toast_success);
        }
    }
}

bool profile_menu_open()
{
    return state().profile.open;
}

bool profile_trigger(const ImRect& rect)
{
    menu_state& m = state();
    return trigger(m.profile, m.target, m.notifications, "profile", rect);
}

profile_choice profile_menu(const ImRect& viewport, float alpha)
{
    panel_state& s = state().profile;
    if (!s.have_trigger)
        return profile_none;

    ImDrawList* dl = ImGui::GetCurrentWindow()->DrawList;

    const float w = px(k_panel_w);
    const float h = px(k_pad) * 2.f + px(k_account_h) + px(k_rule) +
                    px(k_row) * (float)(k_profile_action_count - 1) + px(k_rule) + px(k_row);

    const float left = ImMin(s.trigger.Min.x, viewport.Max.x - px(16.f) - w);
    const float top = ImMax(s.trigger.Min.y - px(6.f) - h, viewport.Min.y + px(16.f));
    const ImRect panel(ImVec2(left, top), ImVec2(left + w, top + h));

    bool exiting = false;
    float open = 0.f;
    if (!begin_panel(s, panel, k_profile_action_count + 1, &exiting, &open))
        return profile_none;
    open *= alpha;

    panel_surface(dl, panel, open);

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    profile_choice chosen = profile_none;
    int index = 0;
    float y = panel.Min.y + px(k_pad);

    account_block(dl, panel, y, row_at(s, index++, exiting, open));
    y += px(k_account_h);
    rule_at(dl, panel, y, open);
    y += px(k_rule);

    for (int i = 0; i < k_profile_action_count; i++)
    {
        if (k_profile_actions[i].destructive)
        {
            rule_at(dl, panel, y, open);
            y += px(k_rule);
        }

        const row_anim a = row_at(s, index++, exiting, open);
        if (action_row(dl, panel, y, a, k_profile_actions[i], exiting, mouse))
        {
            s.open = false;
            if (k_profile_actions[i].destructive)
                chosen = profile_sign_out;
            else if (i == 0)
                chosen = profile_open_page;
            else if (i == 1)
                chosen = profile_open_notifications;
            else
                chosen = profile_open_preferences;
        }
        y += px(k_row);
    }

    return chosen;
}
} // namespace solace
