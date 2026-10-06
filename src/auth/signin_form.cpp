#include "auth/auth.h"
#include "auth/validation.h"
#include "ui/controls/form_controls.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/shell.h"

#include <string>
#include <string_view>

namespace solace
{

namespace
{
struct sign_in_values
{
    char email[128] = "demo@solace.local";
    char password[128] = "correcthorsebattery";
    bool remember = true;
};

struct sign_in_errors
{
    const char* email = nullptr;
    const char* password = nullptr;

    bool any() const
    {
        return email || password;
    }
};

struct touched_flags
{
    bool email = false, password = false;
};

sign_in_errors validate(const sign_in_values& v)
{
    sign_in_errors e;

    if (v.email[0] == 0)
        e.email = "Email is required.";
    else if (!auth::is_valid_email(v.email))
        e.email = "Enter a valid email address.";

    if (v.password[0] == 0)
        e.password = "Password is required.";
    else if (!auth::has_minimum_password_length(v.password))
        e.password = "Use at least 8 characters.";

    return e;
}

constexpr float k_message_duration = 0.2f;

const char* k_title = "Welcome back";
const char* k_description = "Sign in with wrong@example.com to see the failure state.";
const char* k_footer_lead = "New here? ";
const char* k_footer_link = "Create an account";

constexpr float k_request_duration = 1.4f;

bool credentials_rejected(const char* email)
{
    return std::string_view(email).find("wrong") != std::string_view::npos;
}

struct form_state
{
    sign_in_values values;
    touched_flags touched;
    bool reveal_password = false;

    button_state status = btn_idle;
    float request_timer = 0.f;
    bool request_running = false;
    std::string error_message;

    input_state in_email, in_password;
    checkbox_state cb_remember;
    stateful_button_state submit;

    mo::presence form_error_presence;
    mo::presence reset_presence;

    mo::spring h_reset, h_form_error;
    float reset_h = 0.f, form_error_h = 0.f;
    std::string form_error_text;
    std::string reset_text;
};

form_state& state()
{
    static form_state s;
    return s;
}
} // namespace

void signin_reset()
{
    state() = form_state();
}

auth_action signin_screen()
{
    form_state& s = state();
    const auth_view_effect& fx = auth_effect();
    const float dt = fx.interactive ? ImGui::GetIO().DeltaTime : 0.f;
    auth_action action = auth_action::none;

    const sign_in_errors errors = validate(s.values);

    auto shown = [&](const char* error, bool is_touched) -> const char*
    { return is_touched ? error : nullptr; };

    const char* err_email = shown(errors.email, s.touched.email);
    const char* err_password = shown(errors.password, s.touched.password);

    const bool submitting = (s.status == btn_loading);

    if (s.request_running)
    {
        s.request_timer += dt;
        if (s.request_timer >= k_request_duration)
        {
            s.request_running = false;
            if (credentials_rejected(s.values.email))
            {
                s.status = btn_error;
                s.error_message = "Those credentials did not match.";
            }
            else
            {
                s.status = btn_success;
                s.error_message.clear();
                action = auth_action::done;
            }
        }
    }

    input_desc d_email;
    d_email.label = "Email";
    d_email.placeholder = "you@example.com";
    d_email.buf = s.values.email;
    d_email.buf_size = IM_ARRAYSIZE(s.values.email);
    d_email.left = icon_mail;
    d_email.error = err_email;
    d_email.success = !errors.email && s.touched.email;
    d_email.disabled = submitting;

    input_desc d_password;
    d_password.label = "Password";
    d_password.placeholder = "Your password";
    d_password.buf = s.values.password;
    d_password.buf_size = IM_ARRAYSIZE(s.values.password);
    d_password.left = icon_lock;
    d_password.mask = true;
    d_password.reveal_toggle = true;
    d_password.reveal = &s.reveal_password;
    d_password.error = err_password;
    d_password.disabled = submitting;

    const char* submit_label = s.status == btn_loading   ? "Signing in"
                               : s.status == btn_success ? "Signed in"
                               : s.status == btn_error   ? "Try again"
                                                         : "Sign in";

    input_update(s.in_email, d_email, dt);
    input_update(s.in_password, d_password, dt);
    checkbox_update(s.cb_remember, s.values.remember, dt);
    stateful_button_update(s.submit, s.status, submit_label, dt);

    if (!s.error_message.empty())
        s.form_error_text = s.error_message;
    s.form_error_presence.update(!s.error_message.empty(), dt, k_message_duration);
    s.reset_presence.update(!s.reset_text.empty(), dt, k_message_duration);

    const float content_w = px(max_w_sm - 2.f - sp_6 * 2.f);

    const int description_lines =
        wrapped_line_count(font_regular(text_sm), k_description, content_w);
    const float description_h = px(leading_sm) * (float)description_lines;

    const float k_reset_full = px(sp_1_5) + px(leading_xs);
    const float k_form_error_full = px(sp_5) + px(1.f + 8.f + leading_xs + 8.f + 1.f);

    s.reset_h =
        s.h_reset.to(s.reset_presence.mounted && !s.reset_presence.exiting ? k_reset_full : 0.f,
                     mo::SPRING_LAYOUT, dt);
    s.form_error_h = s.h_form_error.to(
        s.form_error_presence.mounted && !s.form_error_presence.exiting ? k_form_error_full : 0.f,
        mo::SPRING_LAYOUT, dt);

    float height = px(1.f + sp_6);
    height += px(leading_xl + sp_1) + description_h;
    height += px(sp_5);

    height += input_height(s.in_email) + px(sp_4) + input_height(s.in_password);
    height += s.reset_h;
    height += px(sp_5);

    height += px(leading_sm);
    height += s.form_error_h;

    height += px(sp_5);
    height += px(sp_12);
    height += px(sp_5) + px(leading_sm);
    height += px(sp_6 + 1.f);

    const ImVec2 card_size =
        shell::animate_size(ImVec2(px(max_w_sm + auth_layout::stage_width), height));
    const auth_layout::frame frame = auth_layout::begin("SignInForm", card_size, fx.interactive);
    {
        ImDrawList* dl = frame.draw_list;
        const ImRect& card = frame.card;

        const int content_vtx_begin = dl->VtxBuffer.Size;

        // Offset the layout origin, not the finished vertices. Nested clip rects
        // then travel with reset/error content during the transition.
        const float x = card.Min.x + px(1.f + sp_6) + fx.offset.x;
        float y = card.Min.y + px(1.f + sp_6) + fx.offset.y;

        ImFont* title_font = font_semibold(text_xl);
        draw_text_tracked(dl, title_font, ImVec2(x, y + line_top(title_font, px(leading_xl))),
                          c_foreground, k_title, px(text_xl * tracking_tight));
        y += px(leading_xl + sp_1);

        draw_text_wrapped(dl, font_regular(text_sm), ImVec2(x, y), c_muted_foreground,
                          k_description, content_w, px(leading_sm));
        y += description_h;

        y += px(sp_5);

        bool blurred = false;

        bool changed = input_draw("email", s.in_email, d_email, ImVec2(x, y), content_w, &blurred);
        if (blurred)
            s.touched.email = true;
        y += input_height(s.in_email) + px(sp_4);

        blurred = false;
        changed |=
            input_draw("password", s.in_password, d_password, ImVec2(x, y), content_w, &blurred);
        if (blurred)
            s.touched.password = true;
        y += input_height(s.in_password);

        if (s.reset_h > 0.5f && !s.reset_text.empty())
        {
            const bool exiting = s.reset_presence.exiting;
            const float p = mo::EASE_OUT_NAMED(
                ImClamp((exiting ? s.reset_presence.out : s.reset_presence.in) / k_message_duration,
                        0.f, 1.f));
            const float opacity = exiting ? 1.f - p : p;

            const float open = s.reset_h;
            dl->PushClipRect(ImVec2(x, y), ImVec2(x + content_w, y + open), true);

            const float top = y + open - k_reset_full;
            ImFont* f = font_regular(text_xs);
            draw_text(dl, f, ImVec2(x, top + px(sp_1_5) + line_top(f, px(leading_xs))),
                      mo::with_alpha(c_success, opacity), s.reset_text.c_str());

            dl->PopClipRect();
            y += open;
        }

        y += px(sp_5);

        {
            checkbox_draw("remember", s.cb_remember, &s.values.remember, "Remember me",
                          ImVec2(x, y), submitting);

            ImFont* link_font = font_medium(text_sm);
            const char* forgot = "Forgot password?";
            const float lw = text_width(link_font, forgot);

            if (link("forgot", dl, link_font,
                     ImVec2(x + content_w - lw, y + line_top(link_font, px(leading_sm))), forgot,
                     c_foreground, 1.f))
            {
                s.reset_text = "Reset link sent to ";
                s.reset_text += s.values.email[0] ? s.values.email : "your inbox";
                s.reset_text += ".";
            }

            y += px(leading_sm);
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

            dl->AddRectFilled(bmin, bmax, mo::with_alpha(c_destructive, 0.1f * opacity), px(16.f));
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
            s.touched = {true, true};

            if (!validate(s.values).any())
            {
                s.status = btn_loading;
                s.request_running = true;
                s.request_timer = 0.f;
                s.error_message.clear();
                s.reset_text.clear();
            }
        }

        y += px(sp_12);
        y += px(sp_5);

        {
            ImFont* lead_font = font_regular(text_sm);
            ImFont* link_font = font_medium(text_sm);

            const float lead_w = text_width(lead_font, k_footer_lead);
            const float link_w = text_width(link_font, k_footer_link);

            const float footer_x = x + (content_w - (lead_w + link_w)) * 0.5f;
            const float footer_y = y + line_top(lead_font, px(leading_sm));

            draw_text(dl, lead_font, ImVec2(footer_x, footer_y), c_muted_foreground, k_footer_lead);
            if (link("to-signup", dl, link_font, ImVec2(footer_x + lead_w, footer_y), k_footer_link,
                     c_foreground, 1.f))
                action = auth_action::switch_form;
        }

        if (changed && (s.status == btn_success || s.status == btn_error))
        {
            s.status = btn_idle;
            s.error_message.clear();
        }

        auth_apply_effect(dl, content_vtx_begin);
    }
    auth_layout::end();

    return action;
}
} // namespace solace
