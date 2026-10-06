#include "application/app.h"

#include "auth/auth.h"
#include "ui/controls/loader.h"
#include "ui/controls/theme_toggle.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/shell.h"

namespace solace
{

namespace
{
enum class screen_id
{
    signup = 0,
    signin,
    legal,
    loading,
    menu,
};

struct app_state
{
    screen_id screen = screen_id::signup;
    float elapsed = 0.f;

    legal_document document = legal_document::terms;
    screen_id document_return = screen_id::signup;

    bool auth_switching = false;
    bool auth_entering = false;
    screen_id auth_target = screen_id::signin;
    int auth_direction = 1;
    float auth_elapsed = 0.f;
    auth_view_effect auth_fx;
};

app_state& state()
{
    static app_state s;
    return s;
}

constexpr float k_loading_duration = 0.5f;

constexpr float k_loader_size = 64.f;

void begin_shell_window()
{
    const ImVec2 size = shell::animate_size(px(shell::width, shell::height));
    ui_runtime::host_size = size;

    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(ImVec2(0, 0));

    ImGui::Begin("Shell", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);

    ui_runtime::apply_style();
}

void begin_auth_switch(app_state& s, screen_id from, screen_id to)
{
    if (s.auth_switching)
        return;

    s.auth_switching = true;
    s.auth_entering = false;
    s.auth_target = to;
    s.auth_direction = to > from ? 1 : -1;
    s.auth_elapsed = 0.f;
    s.auth_fx = {};
    s.auth_fx.interactive = false;

    // A focused field must not keep accepting input while its vertices move.
    ImGui::ClearActiveID();
}

void update_auth_switch(app_state& s, float dt)
{
    constexpr float k_exit_duration = 0.09f;
    constexpr float k_enter_duration = 0.16f;

    if (!s.auth_switching)
    {
        s.auth_fx = {};
        return;
    }

    s.auth_elapsed += dt;
    s.auth_fx.interactive = false;

    if (!s.auth_entering)
    {
        const float p = ImClamp(s.auth_elapsed / k_exit_duration, 0.f, 1.f);
        const float eased = 1.f - mo::EASE_OUT(1.f - p);
        s.auth_fx.opacity = 1.f - eased;
        s.auth_fx.offset = ImVec2(-px(8.f) * (float)s.auth_direction * eased, -px(2.f) * eased);

        if (p >= 1.f)
        {
            s.screen = s.auth_target;
            s.auth_entering = true;
            // Start a few milliseconds into the entrance so the handoff never
            // produces a completely blank content pane on a boundary frame.
            s.auth_elapsed = 0.004f;
            const float enter_p = ImClamp(s.auth_elapsed / k_enter_duration, 0.f, 1.f);
            const float enter_eased = mo::EASE_OUT(enter_p);
            s.auth_fx.opacity = enter_eased;
            s.auth_fx.offset = ImVec2(px(10.f) * (float)s.auth_direction * (1.f - enter_eased),
                                      px(3.f) * (1.f - enter_eased));
        }
    }
    else
    {
        const float p = ImClamp(s.auth_elapsed / k_enter_duration, 0.f, 1.f);
        const float eased = mo::EASE_OUT(p);
        s.auth_fx.opacity = eased;
        s.auth_fx.offset =
            ImVec2(px(10.f) * (float)s.auth_direction * (1.f - eased), px(3.f) * (1.f - eased));

        if (p >= 1.f)
        {
            s.auth_switching = false;
            s.auth_entering = false;
            s.auth_fx = {};
        }
    }
}
} // namespace

const auth_view_effect& auth_effect()
{
    return state().auth_fx;
}

void auth_apply_effect(ImDrawList* dl, int first_vertex)
{
    if (!dl)
        return;

    const auth_view_effect& fx = auth_effect();
    const int begin = ImClamp(first_vertex, 0, dl->VtxBuffer.Size);
    const float opacity = ImClamp(fx.opacity, 0.f, 1.f);

    if (opacity >= 0.999f)
        return;

    for (int i = begin; i < dl->VtxBuffer.Size; i++)
    {
        ImDrawVert& vertex = dl->VtxBuffer[i];
        const ImU32 alpha = (vertex.col & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
        const ImU32 faded = (ImU32)(opacity * (float)alpha + 0.5f);
        vertex.col = (vertex.col & ~IM_COL32_A_MASK) | (faded << IM_COL32_A_SHIFT);
    }
}

static void loading_screen(float elapsed)
{
    begin_shell_window();
    {
        const ImRect rect = shell::plate();
        metaballs(ImGui::GetCurrentWindow()->DrawList, rect.GetCenter(), px(k_loader_size), 1.f,
                  elapsed, c_foreground);
    }
    ImGui::End();
}

void application::render_frame()
{
    app_state& s = state();
    const float dt = ImGui::GetIO().DeltaTime;
    s.elapsed += dt;

    theme_tick(dt);
    update_auth_switch(s, dt);

    auto handle = [&](auth_action result, screen_id here, screen_id other)
    {
        switch (result)
        {
        case auth_action::done:
            s.screen = screen_id::loading;
            s.elapsed = 0.f;
            break;
        case auth_action::switch_form:
            begin_auth_switch(s, here, other);
            s.elapsed = 0.f;
            break;
        case auth_action::terms:
        case auth_action::privacy:
            s.document =
                (result == auth_action::terms) ? legal_document::terms : legal_document::privacy;
            s.document_return = here;
            s.screen = screen_id::legal;
            s.elapsed = 0.f;
            break;
        default:
            break;
        }
    };

    switch (s.screen)
    {
    case screen_id::signup:
        handle(signup_screen(), screen_id::signup, screen_id::signin);
        break;

    case screen_id::signin:
        handle(signin_screen(), screen_id::signin, screen_id::signup);
        break;

    case screen_id::legal:
        if (legal_screen(s.document))
        {
            s.screen = s.document_return;
            s.elapsed = 0.f;
        }
        break;

    case screen_id::loading:
        loading_screen(s.elapsed);
        if (s.elapsed >= k_loading_duration)
        {
            s.screen = screen_id::menu;
            s.elapsed = 0.f;
        }
        break;

    case screen_id::menu:

        if (menu_screen(mo::EASE_OUT(ImClamp(s.elapsed / 0.18f, 0.f, 1.f))))
        {
            signup_reset();
            signin_reset();
            s.screen = screen_id::signin;
            s.elapsed = 0.f;
        }
        break;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    theme_reveal_draw(ImGui::GetForegroundDrawList(),
                      ImRect(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y)));
}
} // namespace solace
