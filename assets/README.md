# Runtime assets

The interface loads its slides, avatars, target logos, and provider marks from this
directory. Pondot supplied the project media and authorized its redistribution with
Solace. The media is not relicensed under the repository's MIT license, and third-party
product marks remain the property of their respective owners.

The application remains usable when these folders are empty: the slide shader is omitted,
while avatar, target-logo, and provider artwork falls back to built-in UI treatment. The
authentication stage itself remains present with its product and testimonial treatment.

## Packaging

Runtime asset discovery begins beside the executable and searches up to three parent
directories for `assets/`. A packaged release should therefore ship the complete `assets`
directory next to `Solace.exe`, or set `SOLACE_SLIDES`, `SOLACE_AVATARS`, `SOLACE_LOGOS`,
and `SOLACE_BRANDS` to explicit locations.

Slides are decoded to a maximum 1024-pixel long edge. Avatars, target logos, and provider
marks are normalized to a maximum 128-pixel working size. These limits avoid decoding
full-resolution images on every launch when the UI displays them much smaller.
