# Dear ImGui changes

Solace vendors Dear ImGui 1.92.9b and keeps a small set of local text-rendering changes.

They support:

- pair spacing shared by measurement and drawing
- integer glyph placement for small text
- grid-fitted FreeType advances
- a centered, consistently spaced `U+2022` password mask
- mipmap sampling in the DirectX 11 backend

Local changes are marked in the vendored source with `// [Solace]`.

When upgrading Dear ImGui:

1. start from the official Dear ImGui release;
2. search the current vendor tree for `[Solace]`;
3. reapply only the required changes;
4. build Debug and Release;
5. check normal text, wrapped text, and text input.

Vendored Dear ImGui files remain under Dear ImGui's MIT license.
