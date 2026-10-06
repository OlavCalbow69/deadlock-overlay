#include "auth/auth.h"
#include "ui/controls/scroll.h"
#include "ui/foundation/draw.h"
#include "ui/foundation/primitives.h"
#include "ui/screens/shell.h"

namespace solace
{

namespace
{
constexpr float k_card_h = 640.f;

constexpr float k_para_leading = 22.f;
constexpr float k_section_gap = 20.f;
constexpr float k_heading_gap = 8.f;

struct section
{
    const char* heading;
    const char* body;
};

const section k_terms[] = {
    {"Demo status",
     "Solace is an offline interface demonstration. It does not create accounts, connect to an "
     "online service, or grant access to a product."},
    {"Local input",
     "Text entered in the demonstration remains in the running process for the current session. "
     "Do not enter a real email address, password, or other sensitive information."},
    {"Acceptable use",
     "Use the project and its assets only where you have the necessary rights. Do not present the "
     "demonstration as a working authentication or payment service."},
    {"Source and media",
     "The source code is available under the repository's MIT license. Bundled media and provider "
     "marks remain subject to the notices shipped with the project."},
    {"No billing",
     "Solace has no plans, subscriptions, purchases, or payment processing. Any product or account "
     "language shown elsewhere is sample interface content."},
    {"Availability",
     "The project is provided as a demonstration without a hosted service or availability "
     "commitment. Features and sample content may change between versions."},
    {"Warranty",
     "The software is provided as-is, without warranty, to the extent permitted by the MIT "
     "license and applicable law."},
};

const section k_privacy[] = {
    {"No collection",
     "Solace does not contact a server, create an account, use analytics, or transmit the contents "
     "of its demonstration fields."},
    {"Input fields",
     "Email and password values exist only in the application's memory while it is running. They "
     "are sample inputs and should not contain real credentials."},
    {"Diagnostics",
     "Local diagnostic logs record startup, shutdown, and rendering failures. They do not record "
     "the contents of email, password, or other form fields."},
    {"Local assets",
     "Images and interface data are loaded from the packaged assets directory. Solace does not "
     "upload those files or inspect unrelated files on the computer."},
    {"No cookies",
     "This native Windows demonstration does not use browser cookies, advertising identifiers, or "
     "cross-application tracking."},
    {"Adaptation",
     "Anyone connecting this interface to a real service must replace this demonstration text with "
     "a policy that accurately describes that service before collecting user data."},
    {"Getting in touch",
     "For this local demonstration, privacy@solace.example is a non-deliverable placeholder "
     "address. Replace it before adapting this screen for a real service or privacy request."},
};

struct doc
{
    const char* title;
    const char* updated;
    const section* sections;
    int count;
};

const doc k_docs[] = {
    {"Terms of Service", "Last updated 23 August 2026", k_terms, IM_ARRAYSIZE(k_terms)},
    {"Privacy Policy", "Last updated 23 August 2026", k_privacy, IM_ARRAYSIZE(k_privacy)},
};

struct legal_state
{
    struct back_button_state
    {
        mo::spring hover;
        mo::spring press;
        mo::spring arrow_shift;
    };

    smooth_scroll body[IM_ARRAYSIZE(k_docs)];
    float content[IM_ARRAYSIZE(k_docs)] = {};
    back_button_state back[IM_ARRAYSIZE(k_docs)];
};

legal_state& state()
{
    static legal_state s;
    return s;
}

bool back_button(ImDrawList* dl, ImGuiWindow* window, legal_state::back_button_state& st,
                 const ImVec2& pos)
{
    constexpr const char* label = "Back";

    ImFont* f = font_medium(text_sm);
    const ImVec2 size = px(84.f, 36.f);
    const ImRect bb(pos, pos + size);

    ImGui::PushID("legal-back");
    const ImGuiID item_id = window->GetID("button");
    ImGui::SetCursorScreenPos(bb.Min);
    ImGui::ItemSize(bb.GetSize());
    const bool visible = ImGui::ItemAdd(bb, item_id);

    bool hovered = false, held = false;
    const bool pressed = visible && ImGui::ButtonBehavior(bb, item_id, &hovered, &held);
    const bool focused = ImGui::IsItemFocused();
    ImGui::PopID();

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const float dt = ImGui::GetIO().DeltaTime;
    const float hover_t = st.hover.to(hovered ? 1.f : 0.f, mo::SPRING_LAYOUT, dt);
    const float press_t = st.press.to(held ? 0.96f : 1.f, mo::SPRING_PRESS, dt);
    const float arrow_shift = st.arrow_shift.to(held      ? -1.5f
                                                : hovered ? -0.5f
                                                          : 0.f,
                                                mo::SPRING_PRESS, dt);

    const ImVec2 center = bb.GetCenter();
    const ImVec2 half = bb.GetSize() * (0.5f * press_t);
    const ImRect surface(center - half, center + half);
    const float rounding = px(10.f) * press_t;

    if (focused)
        dl->AddRect(surface.Min - px(2.f, 2.f), surface.Max + px(2.f, 2.f),
                    mo::with_alpha(c_foreground, 0.28f), rounding + px(2.f), px(1.f),
                    ImDrawFlags_None);

    const float fill_alpha = 0.025f + 0.045f * hover_t + (held ? 0.02f : 0.f);
    dl->AddRectFilled(surface.Min, surface.Max, mo::with_alpha(c_foreground, fill_alpha), rounding);
    dl->AddRect(surface.Min, surface.Max, mo::mix(c_border, c_border_strong, hover_t), rounding,
                px(1.f), ImDrawFlags_None);

    const ImU32 ink = mo::mix(c_muted_foreground, c_foreground, hover_t);
    const float icon_box = px(16.f);
    const ImVec2 icon_tl(bb.Min.x + px(12.f) + px(arrow_shift), center.y - icon_box * 0.5f);

    const int rotation_start = draw_utils::rotation_start(dl);
    icons::draw(icons::id::chevron_right, dl, icon_tl, icon_box, ink);
    draw_utils::rotate_vertices(dl, rotation_start, IM_PI,
                                icon_tl + ImVec2(icon_box * 0.5f, icon_box * 0.5f));

    draw_text(dl, f, ImVec2(bb.Min.x + px(36.f), center.y - f->LegacySize * 0.5f), ink, label);

    return pressed;
}
} // namespace

bool legal_screen(legal_document document)
{
    legal_state& s = state();
    const int index =
        ImClamp(static_cast<int>(document), 0, static_cast<int>(IM_ARRAYSIZE(k_docs)) - 1);
    const doc& d = k_docs[index];

    bool back = false;

    const ImVec2 card_size =
        shell::animate_size(ImVec2(px(max_w_sm + auth_layout::stage_width), px(k_card_h)));
    const auth_layout::frame frame = auth_layout::begin("Legal", card_size);
    {
        ImGuiWindow* window = frame.window;
        ImDrawList* dl = frame.draw_list;
        const ImRect& card = frame.card;

        const float x = card.Min.x + px(1.f + sp_6);
        const float content_w = px(max_w_sm - 2.f - sp_6 * 2.f);
        float y = card.Min.y + px(1.f + sp_6);

        if (back_button(dl, window, s.back[index], ImVec2(x, y)))
            back = true;
        y += px(36.f + sp_3);

        ImFont* title_font = font_semibold(text_xl);
        draw_text_tracked(dl, title_font, ImVec2(x, y + line_top(title_font, px(leading_xl))),
                          c_foreground, d.title, px(text_xl * tracking_tight));
        y += px(leading_xl + sp_1);

        ImFont* updated_font = font_regular(text_xs);
        draw_text(dl, updated_font, ImVec2(x, y + line_top(updated_font, px(leading_xs))),
                  c_muted_foreground, d.updated);
        y += px(leading_xs) + px(sp_4);

        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + content_w, y + px(1.f)), c_border);
        y += px(1.f) + px(sp_5);

        const ImRect body(ImVec2(x, y), ImVec2(x + content_w, card.Max.y - px(1.f + sp_6)));

        const float measured = s.content[index] > 0.f ? s.content[index] : body.GetHeight();
        dl->PushClipRect(body.Min, body.Max, true);
        const float scroll = scroll_area(s.body[index], body, measured);

        ImFont* head_font = font_semibold(text_sm);
        ImFont* para_font = font_regular(text_sm);

        float by = body.Min.y - scroll;
        for (int i = 0; i < d.count; i++)
        {
            draw_text(dl, head_font, ImVec2(x, by + line_top(head_font, px(leading_sm))),
                      c_foreground, d.sections[i].heading);
            by += px(leading_sm) + px(k_heading_gap);

            const int lines = wrapped_line_count(para_font, d.sections[i].body, content_w);
            draw_text_wrapped(dl, para_font, ImVec2(x, by), c_muted_foreground, d.sections[i].body,
                              content_w, px(k_para_leading));
            by += px(k_para_leading) * (float)lines;

            if (i + 1 < d.count)
                by += px(k_section_gap);
        }

        s.content[index] = (by + px(sp_4)) - (body.Min.y - scroll);
        scrollbar(dl, body, measured, scroll, 1.f);
        dl->PopClipRect();
    }
    auth_layout::end();

    return back;
}
} // namespace solace
