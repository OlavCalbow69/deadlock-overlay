#include "application/brand.h"
#include "assets/avatars.h"
#include "auth/auth.h"
#include "auth/validation.h"
#include "ui/controls/form_controls.h"
#include "ui/controls/widgets.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/shell.h"

#include <cstring>
#include <string>
#include <string_view>

namespace solace
{

namespace
{

struct sign_up_values
{
    sign_up_values()
    {
        ImStrncpy(name, brand::user_name, IM_ARRAYSIZE(name));
    }

    char name[128] = {};
    char email[128] = "demo@solace.local";
    char password[128] = "correcthorsebattery";
    char confirm_password[128] = "correcthorsebattery";
    bool terms = true;
};

struct sign_up_errors
{
    const char* name = nullptr;
    const char* email = nullptr;
    const char* password = nullptr;
    const char* confirm_password = nullptr;
    const char* terms = nullptr;

    bool any() const
    {
        return name || email || password || confirm_password || terms;
    }
};

struct touched_flags
{
    bool name = false, email = false, password = false, confirm_password = false, terms = false;
};

const char* k_strength_labels[] = {"Too short", "Weak", "Fair", "Good", "Strong"};
const ImU32 k_strength_colors[] = {c_destructive, c_destructive, c_amber_500, c_amber_400,
                                   c_success};

sign_up_errors default_validate(const sign_up_values& v)
{
    sign_up_errors e;

    if (auth::is_blank(v.name))
        e.name = "Enter your name.";

    if (auth::is_blank(v.email))
        e.email = "Enter your email.";
    else if (!auth::is_valid_email(v.email))
        e.email = "That doesn't look like an email address.";

    if (v.password[0] == '\0')
        e.password = "Choose a password.";
    else if (!auth::has_minimum_password_length(v.password))
        e.password = "Use at least 8 characters.";

    if (v.confirm_password[0] == '\0')
        e.confirm_password = "Confirm your password.";
    else if (std::strcmp(v.confirm_password, v.password) != 0)
        e.confirm_password = "Passwords don't match.";

    if (!v.terms)
        e.terms = "Accept the terms to continue.";

    return e;
}

enum class signup_step : int
{
    credentials,
    profile,
    verification,
};

constexpr int signup_step_index(signup_step value) noexcept
{
    return static_cast<int>(value);
}

struct form_state
{
    sign_up_values values;
    touched_flags touched;
    bool reveal_password = false;

    button_state status = btn_idle;
    float request_timer = 0.f;
    bool request_running = false;
    std::string error_message;

    signup_step step = signup_step::credentials;

    input_state in_name, in_email, in_password, in_confirm, in_preset;
    char preset[64] = "Competitive";
    char code[8] = {};
    otp_status code_status = otp_idle;
    float code_timer = 1e6f;
    checkbox_state cb_terms;
    stateful_button_state submit;

    mo::presence strength_presence;
    mo::presence terms_error_presence;
    mo::presence form_error_presence;

    std::string terms_error_text;
    std::string form_error_text;

    mo::spring strength_bar[4];

    mo::spring h_strength, h_terms_error, h_form_error, h_confirm;
    float strength_h = 0.f, terms_error_h = 0.f, form_error_h = 0.f, confirm_h = 0.f;
};

form_state& state()
{
    static form_state s;
    return s;
}
} // namespace

void signup_reset()
{
    state() = form_state();
}

namespace
{

constexpr float k_strength_duration = 0.18f;
constexpr float k_message_duration = 0.2f;

struct provider
{
    const char* label;
    icons::id icon;
    const char* mark;
};
const provider k_providers[] = {
    {"Google", icons::id::circle_user_round, "google"},
    {"GitHub", icons::id::command, "github"},
    {"SSO", icons::id::building_2, nullptr},
};

bool provider_button(const char* id, ImDrawList* dl, const ImVec2& pos, float w, const provider& p,
                     float alpha)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::PushID(id);
    const ImGuiID item = window->GetID("prov");
    ImGui::PopID();

    const ImRect bb(pos, ImVec2(pos.x + w, pos.y + px(42.f)));
    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(ImVec2(0.f, 0.f));
    ImGui::ItemAdd(bb, item);

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, item, &hovered, &held);

    dl->AddRectFilled(bb.Min, bb.Max,
                      mo::with_alpha(c_foreground, (held      ? 0.08f
                                                    : hovered ? 0.05f
                                                              : 0.02f) *
                                                       alpha),
                      px(14.f));
    dl->AddRect(ImVec2(bb.Min.x + px(0.5f), bb.Min.y + px(0.5f)),
                ImVec2(bb.Max.x - px(0.5f), bb.Max.y - px(0.5f)),
                mo::with_alpha(hovered ? c_border_strong : c_border, alpha), px(14.f), px(1.f),
                ImDrawFlags_None);

    ImFont* f = font_medium(text_sm);
    const float icon = px(16.f);
    const float tw = ImMin(text_width(f, p.label), w - icon - px(20.f));
    const float run = icon + px(7.f) + tw;
    const float lx = bb.GetCenter().x - run * 0.5f;

    const ImU32 ink = mo::with_alpha(hovered ? c_foreground : c_muted_foreground, alpha);
    const ImVec2 icon_at(lx, bb.GetCenter().y - icon * 0.5f);

    const ImTextureID tex = p.mark ? avatars::brand(p.mark) : ImTextureID_Invalid;
    if (tex != ImTextureID_Invalid)
        dl->AddImage(tex, icon_at, ImVec2(icon_at.x + icon, icon_at.y + icon), ImVec2(0, 0),
                     ImVec2(1, 1), ink);
    else
        icons::draw(p.icon, dl, icon_at, icon, ink);

    draw_text_ellipsis(dl, f, ImVec2(lx + icon + px(7.f), bb.GetCenter().y - f->LegacySize * 0.5f),
                       mo::with_alpha(c_foreground, alpha), p.label, tw + px(1.f));

    return pressed;
}

void or_rule(ImDrawList* dl, const ImVec2& pos, float w, float alpha)
{
    ImFont* f = font_regular(text_xs);
    const char* word = "or";
    const float tw = text_width(f, word);
    const float mid = pos.x + w * 0.5f;
    const float gap = px(10.f);

    dl->AddRectFilled(ImVec2(pos.x, pos.y - px(0.5f)),
                      ImVec2(mid - tw * 0.5f - gap, pos.y + px(0.5f)),
                      mo::with_alpha(c_border, alpha));
    dl->AddRectFilled(ImVec2(mid + tw * 0.5f + gap, pos.y - px(0.5f)),
                      ImVec2(pos.x + w, pos.y + px(0.5f)), mo::with_alpha(c_border, alpha));

    draw_text(dl, f, ImVec2(mid - tw * 0.5f, pos.y - f->LegacySize * 0.5f),
              mo::with_alpha(c_muted_foreground, alpha), word);
}

const char* const k_step_titles[] = {
    "Create your account",
    "Name your preset",
    "Check your email",
};
const char* const k_step_blurbs[] = {
    "Sign up with used@solace.local to see the failure state.",
    "The preset that loads by default. You can add more later.",
    "We sent a six digit code. It is good for ten minutes.",
};

const char* k_footer_lead = "Already have an account? ";
const char* k_footer_link = "Sign in";

constexpr float k_request_duration = 1.4f;

bool email_is_taken(const char* email)
{
    return std::string_view(email).find("used") != std::string_view::npos;
}
} // namespace

auth_action signup_screen()
{
    form_state& s = state();
    bool just_succeeded = false;
    auth_action action = auth_action::none;
    const auth_view_effect& fx = auth_effect();
    const float dt = fx.interactive ? ImGui::GetIO().DeltaTime : 0.f;

    const sign_up_errors errors = default_validate(s.values);

    auto shown = [&](const char* error, bool is_touched) -> const char*
    { return is_touched ? error : nullptr; };

    const char* err_name = shown(errors.name, s.touched.name);
    const char* err_email = shown(errors.email, s.touched.email);
    const char* err_password = shown(errors.password, s.touched.password);
    const char* err_confirm = shown(errors.confirm_password, s.touched.confirm_password);
    const char* err_terms = shown(errors.terms, s.touched.terms);

    const bool ok_name = s.touched.name && !errors.name && s.values.name[0] != '\0';
    const bool ok_email = s.touched.email && !errors.email && s.values.email[0] != '\0';
    const bool ok_confirm = s.touched.confirm_password && !errors.confirm_password &&
                            s.values.confirm_password[0] != '\0';

    const int strength = static_cast<int>(auth::evaluate_password(s.values.password));
    const bool show_strength = s.values.password[0] != '\0';
    const bool submitting = s.status == btn_loading;

    if (s.request_running)
    {
        s.request_timer += dt;
        if (s.request_timer >= k_request_duration)
        {
            s.request_running = false;
            if (email_is_taken(s.values.email))
            {
                s.status = btn_error;
                s.error_message = "That email is already registered.";
            }
            else
            {
                s.status = btn_success;
                s.error_message.clear();
                just_succeeded = true;
            }
        }
    }

    input_desc d_name;
    d_name.label = "Name";
    d_name.placeholder = brand::user_name;
    d_name.buf = s.values.name;
    d_name.buf_size = IM_ARRAYSIZE(s.values.name);
    d_name.left = icon_user;
    d_name.error = err_name;
    d_name.success = ok_name;
    d_name.disabled = submitting;

    input_desc d_preset;
    d_preset.label = "Preset name";
    d_preset.placeholder = "Competitive";
    d_preset.buf = s.preset;
    d_preset.buf_size = IM_ARRAYSIZE(s.preset);
    d_preset.left = icon_user;
    d_preset.disabled = submitting;

    input_desc d_email;
    d_email.label = "Email";
    d_email.placeholder = "you@example.com";
    d_email.buf = s.values.email;
    d_email.buf_size = IM_ARRAYSIZE(s.values.email);
    d_email.left = icon_mail;
    d_email.error = err_email;
    d_email.success = ok_email;
    d_email.disabled = submitting;

    input_desc d_password;
    d_password.label = "Password";
    d_password.placeholder = "At least 8 characters";
    d_password.buf = s.values.password;
    d_password.buf_size = IM_ARRAYSIZE(s.values.password);
    d_password.left = icon_lock;
    d_password.mask = true;
    d_password.reveal_toggle = true;
    d_password.reveal = &s.reveal_password;
    d_password.error = err_password;
    d_password.disabled = submitting;

    input_desc d_confirm;
    d_confirm.label = "Confirm password";
    d_confirm.placeholder = "Re-enter your password";
    d_confirm.buf = s.values.confirm_password;
    d_confirm.buf_size = IM_ARRAYSIZE(s.values.confirm_password);
    d_confirm.left = icon_lock;
    d_confirm.mask = true;
    d_confirm.reveal = &s.reveal_password;
    d_confirm.error = err_confirm;
    d_confirm.success = ok_confirm;
    d_confirm.disabled = submitting;

    const char* submit_label = s.status == btn_loading               ? "Creating account"
                               : s.status == btn_success             ? "Account created"
                               : s.status == btn_error               ? "Try again"
                               : s.step == signup_step::credentials  ? "Continue"
                               : s.step == signup_step::verification ? "Verify code"
                                                                     : "Create account";

    input_update(s.in_name, d_name, dt);
    input_update(s.in_email, d_email, dt);
    input_update(s.in_password, d_password, dt);
    input_update(s.in_confirm, d_confirm, dt);
    checkbox_update(s.cb_terms, s.values.terms, dt);
    stateful_button_update(s.submit, s.status, submit_label, dt);

    s.strength_presence.update(show_strength, dt, k_strength_duration);

    if (err_terms)
        s.terms_error_text = err_terms;
    s.terms_error_presence.update(err_terms != nullptr, dt, k_message_duration);

    if (!s.error_message.empty())
        s.form_error_text = s.error_message;
    s.form_error_presence.update(!s.error_message.empty(), dt, k_message_duration);

    const float k_strength_full = px(sp_2) + px(4.f + sp_1_5 + leading_xs);
    const float k_terms_error_full = px(sp_1_5 + leading_xs);
    const float k_form_error_full = px(sp_5) + px(1.f + 8.f + leading_xs + 8.f + 1.f);

    s.strength_h = s.h_strength.to(
        s.strength_presence.mounted && !s.strength_presence.exiting ? k_strength_full : 0.f,
        mo::SPRING_LAYOUT, dt);
    s.terms_error_h = s.h_terms_error.to(
        s.terms_error_presence.mounted && !s.terms_error_presence.exiting ? k_terms_error_full
                                                                          : 0.f,
        mo::SPRING_LAYOUT, dt);
    s.form_error_h = s.h_form_error.to(
        s.form_error_presence.mounted && !s.form_error_presence.exiting ? k_form_error_full : 0.f,
        mo::SPRING_LAYOUT, dt);

    const bool want_confirm = s.values.password[0] != 0;
    const float k_confirm_full = px(sp_4) + input_height(s.in_confirm);
    s.confirm_h = s.h_confirm.to(want_confirm ? k_confirm_full : 0.f, mo::SPRING_LAYOUT, dt);

    const float content_w = px(max_w_sm - 2.f - sp_6 * 2.f);

    const int description_lines = wrapped_line_count(
        font_regular(text_sm), k_step_blurbs[signup_step_index(s.step)], content_w);
    const float description_h = px(leading_sm) * (float)description_lines;

    float height = px(1.f + sp_6);
    height += px(leading_xl + sp_1) + description_h;
    height += px(sp_5);
    if (s.step == signup_step::credentials)
    {
        height += px(42.f) + px(sp_4) + px(1.f) + px(sp_4);

        float fields_h = input_height(s.in_email) + px(sp_4) + input_height(s.in_password);
        fields_h += s.strength_h;
        fields_h += s.confirm_h;

        height += fields_h;
        height += px(sp_5);

        height += px(leading_sm) + s.terms_error_h;
        height += s.form_error_h;
        height += px(sp_4) + px(leading_sm);
    }
    else if (s.step == signup_step::profile)
    {
        height += input_height(s.in_name) + px(sp_4) + input_height(s.in_preset);
        height += s.form_error_h;
    }
    else
    {
        height += px(otp_cell_h);
    }

    height += px(sp_5);
    height += px(sp_12);
    height += px(sp_5) + px(leading_sm);
    height += px(sp_6 + 1.f);

    const ImVec2 card_size =
        shell::animate_size(ImVec2(px(max_w_sm + auth_layout::stage_width), height));
    const auth_layout::frame frame = auth_layout::begin("SignUpForm", card_size, fx.interactive);
    {
        ImDrawList* dl = frame.draw_list;
        const ImRect& card = frame.card;

        const int content_vtx_begin = dl->VtxBuffer.Size;

        // Offset the layout origin, not the finished vertices. Nested clip rects
        // then travel with password/validation content during the transition.
        const float x = card.Min.x + px(1.f + sp_6) + fx.offset.x;
        float y = card.Min.y + px(1.f + sp_6) + fx.offset.y;

        ImFont* title_font = font_semibold(text_xl);
        draw_text_tracked(dl, title_font, ImVec2(x, y + line_top(title_font, px(leading_xl))),
                          c_foreground, k_step_titles[signup_step_index(s.step)],
                          px(text_xl * tracking_tight));
        y += px(leading_xl + sp_1);

        draw_text_wrapped(dl, font_regular(text_sm), ImVec2(x, y), c_muted_foreground,
                          k_step_blurbs[signup_step_index(s.step)], content_w, px(leading_sm));
        y += description_h;

        y += px(sp_5);

        bool changed = false;

        if (s.step == signup_step::credentials)
        {

            {
                const float gap = px(sp_2);
                const float pw = (content_w - gap * 2.f) / 3.f;

                for (int i = 0; i < IM_ARRAYSIZE(k_providers); i++)
                {
                    char pid[16];
                    ImFormatString(pid, IM_ARRAYSIZE(pid), "prov%d", i);
                    if (provider_button(pid, dl, ImVec2(x + (pw + gap) * (float)i, y), pw,
                                        k_providers[i], 1.f))
                        toast("Redirecting", k_providers[i].label, toast_loading);
                }

                y += px(42.f) + px(sp_4);
                or_rule(dl, ImVec2(x, y), content_w, 1.f);
                y += px(1.f) + px(sp_4);
            }

            {
                bool field_blurred = false;
                changed |= input_draw("email", s.in_email, d_email, ImVec2(x, y), content_w,
                                      &field_blurred);
                if (field_blurred)
                    s.touched.email = true;
            }
            y += input_height(s.in_email) + px(sp_4);

            {
                bool field_blurred = false;
                changed |= input_draw("password", s.in_password, d_password, ImVec2(x, y),
                                      content_w, &field_blurred);
                if (field_blurred)
                    s.touched.password = true;
            }
            y += input_height(s.in_password);

            if (s.strength_h > 0.5f)
            {
                const bool exiting = s.strength_presence.exiting;
                const float p = mo::EASE_OUT(
                    ImClamp((exiting ? s.strength_presence.out : s.strength_presence.in) /
                                k_strength_duration,
                            0.f, 1.f));
                const float opacity = exiting ? 1.f - p : p;
                const float shift = -4.f * (exiting ? p : 1.f - p);

                const float open = s.strength_h;
                dl->PushClipRect(ImVec2(x, y), ImVec2(x + content_w, y + open), true);

                const float top = y + open - k_strength_full + px(sp_2);
                const float bar_y = top + px(shift);
                const float gap = px(sp_1_5);
                const float bar_w = (content_w - px(sp_1) * 2.f - gap * 3.f) / 4.f;
                const float bar_h = px(4.f);

                for (int i = 0; i < 4; i++)
                {
                    const float bx = x + px(sp_1) + (bar_w + gap) * (float)i;
                    const ImVec2 bmin(bx, bar_y);
                    const ImVec2 bmax(bx + bar_w, bar_y + bar_h);

                    dl->AddRectFilled(bmin, bmax,
                                      mo::with_alpha(c_muted_foreground, 0.2f * opacity),
                                      bar_h * 0.5f);

                    const float sx =
                        s.strength_bar[i].to(i < strength ? 1.f : 0.f, mo::SPRING_LAYOUT, dt);
                    if (sx > 0.001f)
                        dl->AddRectFilled(bmin, ImVec2(bmin.x + bar_w * sx, bmax.y),
                                          mo::with_alpha(k_strength_colors[strength], opacity),
                                          bar_h * 0.5f);
                }

                char line[64];
                ImFormatString(line, IM_ARRAYSIZE(line), "Password strength: %s",
                               k_strength_labels[strength]);
                ImFont* meter_font = font_regular(text_xs);
                draw_text(dl, meter_font,
                          ImVec2(x + px(sp_1),
                                 bar_y + bar_h + gap + line_top(meter_font, px(leading_xs))),
                          mo::with_alpha(c_muted_foreground, opacity), line);

                dl->PopClipRect();
                y += open;
            }

            if (s.confirm_h > 0.5f)
            {
                const float open = s.confirm_h;
                dl->PushClipRect(ImVec2(x, y), ImVec2(x + content_w, y + open), true);

                const float top = y + open - k_confirm_full + px(sp_4);
                bool field_blurred = false;
                changed |= input_draw("confirm", s.in_confirm, d_confirm, ImVec2(x, top), content_w,
                                      &field_blurred);
                if (field_blurred)
                    s.touched.confirm_password = true;

                dl->PopClipRect();
                y += open;
            }

            y += px(sp_5);

            static const char* k_terms_label = "I agree to the Terms and Privacy Policy";
            static const char* k_terms_lead = "I agree to the ";
            static const char* k_terms_word = "Terms";
            static const char* k_terms_mid = "I agree to the Terms and ";
            static const char* k_privacy_word = "Privacy Policy";

            {
                ImFont* lf = font_regular(text_sm);
                const ImVec2 label_pos(x + px(20.f) + px(sp_3), y + line_top(lf, px(leading_sm)));

                const float terms_x = label_pos.x + text_width(lf, k_terms_lead);
                const float privacy_x = label_pos.x + text_width(lf, k_terms_mid);

                if (link_span("terms-link", dl, lf, ImVec2(terms_x, label_pos.y),
                              text_width(lf, k_terms_word), c_foreground, 1.f))
                    action = auth_action::terms;

                if (link_span("privacy-link", dl, lf, ImVec2(privacy_x, label_pos.y),
                              text_width(lf, k_privacy_word), c_foreground, 1.f))
                    action = auth_action::privacy;
            }

            if (checkbox_draw("terms", s.cb_terms, &s.values.terms, k_terms_label, ImVec2(x, y),
                              submitting))
            {
                s.touched.terms = true;
                changed = true;
            }
            y += px(leading_sm);

            if (s.terms_error_h > 0.5f && !s.terms_error_text.empty())
            {
                const bool exiting = s.terms_error_presence.exiting;
                const float p = mo::EASE_OUT_NAMED(
                    ImClamp((exiting ? s.terms_error_presence.out : s.terms_error_presence.in) /
                                k_message_duration,
                            0.f, 1.f));
                const float k = exiting ? p : 1.f - p;

                const float open = s.terms_error_h;
                dl->PushClipRect(ImVec2(x, y), ImVec2(x + content_w, y + open), true);

                const float top = y + open - k_terms_error_full;
                ImFont* terms_err_font = font_regular(text_xs);
                draw_text_blur(dl, terms_err_font,
                               ImVec2(x + px(sp_1), top + px(sp_1_5) +
                                                        line_top(terms_err_font, px(leading_xs)) -
                                                        px(4.f * k)),
                               mo::with_alpha(c_destructive, exiting ? 1.f - p : p),
                               s.terms_error_text.c_str(), px(4.f * k));

                dl->PopClipRect();
                y += open;
            }

            if (s.form_error_h > 0.5f && !s.form_error_text.empty())
            {
                const float open = s.form_error_h;
                dl->PushClipRect(ImVec2(x, y), ImVec2(x + content_w, y + open), true);

                const float y_block = y;
                y = y_block + open - k_form_error_full + px(sp_5);

                const bool exiting = s.form_error_presence.exiting;
                const float p = mo::EASE_OUT_NAMED(
                    ImClamp((exiting ? s.form_error_presence.out : s.form_error_presence.in) /
                                k_message_duration,
                            0.f, 1.f));
                const float opacity = exiting ? 1.f - p : p;
                const float shift = -4.f * (exiting ? p : 1.f - p);

                const float block_h = px(1.f + 8.f + leading_xs + 8.f + 1.f);
                const ImVec2 bmin(x, y + px(shift));
                const ImVec2 bmax(x + content_w, bmin.y + block_h);

                dl->AddRectFilled(bmin, bmax, mo::with_alpha(c_destructive, 0.1f * opacity),
                                  px(16.f));
                dl->AddRect(ImVec2(bmin.x + px(0.5f), bmin.y + px(0.5f)),
                            ImVec2(bmax.x - px(0.5f), bmax.y - px(0.5f)),
                            mo::with_alpha(c_destructive, 0.3f * opacity), px(16.f), px(1.f),
                            ImDrawFlags_None);

                ImFont* form_err_font = font_regular(text_xs);
                draw_text(dl, form_err_font,
                          ImVec2(bmin.x + px(1.f + sp_3),
                                 bmin.y + px(1.f + 8.f) + line_top(form_err_font, px(leading_xs))),
                          mo::with_alpha(c_destructive, opacity), s.form_error_text.c_str());

                dl->PopClipRect();
                y = y_block + open;
            }

            y += px(sp_5);

            const bool clicked =
                stateful_button_draw("submit", s.submit, s.status, ImVec2(x, y), content_w, false);

            const bool enter = fx.interactive && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                                                  ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));

            if ((clicked || enter) && !submitting)
            {

                s.touched = {true, true, true, true, true};

                if (!default_validate(s.values).any())
                {
                    s.step = signup_step::profile;
                    s.error_message.clear();
                }
            }

            y += px(sp_12);
            y += px(sp_4);

            {
                static const char* k_code_link = "Email me a code instead";
                ImFont* f = font_medium(text_sm);
                const float w = text_width(f, k_code_link);

                if (link("to-code", dl, f,
                         ImVec2(x + (content_w - w) * 0.5f, y + line_top(f, px(leading_sm))),
                         k_code_link, c_muted_foreground, 1.f))
                {
                    s.step = signup_step::verification;
                    s.code[0] = 0;
                    s.code_status = otp_idle;
                }
            }
            y += px(leading_sm) + px(sp_5) - px(sp_4);
        }
        else if (s.step == signup_step::profile)
        {

            {
                bool field_blurred = false;
                changed |=
                    input_draw("name", s.in_name, d_name, ImVec2(x, y), content_w, &field_blurred);
                if (field_blurred)
                    s.touched.name = true;
            }
            y += input_height(s.in_name) + px(sp_4);

            changed |=
                input_draw("preset", s.in_preset, d_preset, ImVec2(x, y), content_w, nullptr);
            y += input_height(s.in_preset);

            y += px(sp_5);

            const bool go = stateful_button_draw("create", s.submit, s.status, ImVec2(x, y),
                                                 content_w, false) ||
                            (fx.interactive && ImGui::IsKeyPressed(ImGuiKey_Enter, false));

            if (go && !submitting)
            {
                s.touched = {true, true, true, true, true};
                if (!default_validate(s.values).any())
                {
                    s.status = btn_loading;
                    s.request_running = true;
                    s.request_timer = 0.f;
                    s.error_message.clear();
                }
            }

            y += px(sp_12);
            y += px(sp_5);
        }
        else
        {

            const float ow = otp_width(6);
            if (otp_input("code", dl, ImVec2(x + (content_w - ow) * 0.5f, y), s.code, 6,
                          s.code_status, 1.f))
            {

                s.code_status = (std::strcmp(s.code, "424242") == 0) ? otp_success : otp_error;
                s.code_timer = 0.f;
            }
            y += px(otp_cell_h);
            y += px(sp_5);

            if (s.code_status == otp_error)
            {
                s.code_timer += dt;
                if (s.code_timer > 1.1f)
                {
                    s.code[0] = 0;
                    s.code_status = otp_idle;
                }
            }
            else if (s.code_status == otp_success)
            {
                s.code_timer += dt;
                if (s.code_timer > 0.6f && s.status == btn_idle)
                {
                    s.status = btn_loading;
                    s.request_running = true;
                    s.request_timer = 0.f;
                }
            }

            stateful_button_draw("verify", s.submit, s.status, ImVec2(x, y), content_w, false);
            y += px(sp_12);
            y += px(sp_5);
        }

        if (s.step == signup_step::credentials)
        {
            ImFont* lead_font = font_regular(text_sm);
            ImFont* link_font = font_medium(text_sm);

            const float lead_w = text_width(lead_font, k_footer_lead);
            const float link_w = text_width(link_font, k_footer_link);
            const float total = lead_w + link_w;

            const float footer_x = x + (content_w - total) * 0.5f;
            const float footer_y = y + line_top(lead_font, px(leading_sm));

            draw_text(dl, lead_font, ImVec2(footer_x, footer_y), c_muted_foreground, k_footer_lead);
            if (link("to-signin", dl, link_font, ImVec2(footer_x + lead_w, footer_y), k_footer_link,
                     c_foreground, 1.f))
                action = auth_action::switch_form;
        }
        else
        {

            const char* back =
                (s.step == signup_step::verification) ? "Use a password instead" : "Back";
            ImFont* f = font_medium(text_sm);
            const float w = text_width(f, back);

            if (link("back", dl, f,
                     ImVec2(x + (content_w - w) * 0.5f, y + line_top(f, px(leading_sm))), back,
                     c_muted_foreground, 1.f))
            {
                s.step = signup_step::credentials;
                s.status = btn_idle;
                s.code[0] = 0;
                s.code_status = otp_idle;
            }
        }

        if (changed && (s.status == btn_success || s.status == btn_error))
        {
            s.status = btn_idle;
            s.error_message.clear();
        }

        auth_apply_effect(dl, content_vtx_begin);
    }
    auth_layout::end();

    return just_succeeded ? auth_action::done : action;
}
} // namespace solace
