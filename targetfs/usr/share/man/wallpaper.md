# wallpaper

## NAME
wallpaper - set framebuffer background color or image

## SYNOPSIS
`wallpaper <#RRGGBB|image-path>`

## DESCRIPTION
`wallpaper` writes directly to `/dev/fb0` and sets the current framebuffer background.

Inputs:
- Hex color string: `#RRGGBB` (or `RRGGBB`)
- Image path: currently PNG is decoded; JPEG decode module is present but not enabled yet.

Image mode scales the input image to the current framebuffer geometry using nearest-neighbor sampling.

## EXIT STATUS
- `0` success
- `1` failure

## EXAMPLES
Set a flat color:

```sh
wallpaper '#1d2833'
```

Set from an image:

```sh
wallpaper /usr/share/backgrounds/sky.png
```
