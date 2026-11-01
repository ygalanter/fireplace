#!/usr/bin/env python3
"""Regenerates the digit, flame and menu icon resources from tools/source.

The digits only ever reveal one window of the original 144x168 fire animation, so
each flame asset holds just that window, scaled to the digit size of its platforms.
B&W flames are raw 1-bit frames, so showing one takes a flash read and no PNG decoding.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / 'tools' / 'source'
RES = ROOT / 'resources'
FONT = SRC / 'Futura Extra Bold.ttf'

BASE_SIZE = 59         # digit size the fire animation window was designed around
WINDOW_TOP = 14        # first animation row visible through the digits at that size
WINDOW_WIDTH = 130     # width of the animation that the digits can reach at that size
FRAME_MS = 40
BW_WIDTH = 128         # whole bytes per row, so B&W frames can be masked bytewise

# digit size for each display width tag
DIGIT_SIZES = {'144w': BASE_SIZE, '180w': BASE_SIZE, '200w': 110, '260w': 104}

# APNG name -> (digit size, flame width, flame height)
COLOR_FLAMES = {
    'flames_small.png': (BASE_SIZE, 130, 106),
    'flames_emery.png': (110, 184, 198),
    'flames_gabbro.png': (104, 174, 188),
}


def digit_images(size):
    """White-on-black digits cropped to their ink, all sharing a height and a baseline."""
    font = ImageFont.truetype(str(FONT), size)
    images = []
    for digit in '0123456789':
        image = Image.new('1', (size * 2, size * 2), 0)
        draw = ImageDraw.Draw(image)
        draw.fontmode = '1'  # no anti-aliasing: every pixel is either digit or background
        draw.text((size // 2, 0), digit, 1, font=font)
        images.append(image)
    boxes = [image.getbbox() for image in images]
    top = min(box[1] for box in boxes)
    bottom = max(box[3] for box in boxes)
    return [image.crop((box[0], top, box[2], bottom)) for image, box in zip(images, boxes)]


def save_digits():
    out = RES / 'digits'
    out.mkdir(parents=True, exist_ok=True)
    for tag, size in DIGIT_SIZES.items():
        for digit, image in enumerate(digit_images(size)):
            image.save(out / f'{digit}~{tag}.png')


def color_frames(width, height, scale):
    apng = Image.open(SRC / 'fire_animation.png')
    source_width = width / scale
    source_height = height / scale
    left = (WINDOW_WIDTH - source_width) / 2
    for n in range(apng.n_frames):
        apng.seek(n)
        frame = apng.convert('RGB')
        frame.info.clear()  # the source's tRNS chunk has no meaning after converting to RGB
        yield frame, (left, WINDOW_TOP, left + source_width, WINDOW_TOP + source_height)


def pebble_palette():
    levels = (0, 85, 170, 255)
    palette = [c for r in levels for g in levels for b in levels for c in (r, g, b)]
    image = Image.new('P', (1, 1))
    image.putpalette(palette)
    return image


def save_apng(name, size, width, height):
    palette = pebble_palette()
    frames = [frame.resize((width, height), Image.Resampling.BICUBIC, box=box)
                   .quantize(palette=palette, dither=Image.Dither.NONE)
              for frame, box in color_frames(width, height, size / BASE_SIZE)]
    frames[0].save(RES / 'flames' / name, save_all=True, append_images=frames[1:], format='PNG',
                   duration=FRAME_MS, loop=0, disposal=0, blend=0, optimize=True)


def save_bw_flames():
    data = bytearray()
    height = COLOR_FLAMES['flames_small.png'][2]
    for path in sorted((SRC / 'bw').glob('frame-*.png')):
        frame = Image.open(path).convert('L')
        for y in range(WINDOW_TOP, WINDOW_TOP + height):
            for x_byte in range(BW_WIDTH // 8):
                value = 0
                for bit in range(8):  # framebuffer bit order: leftmost pixel is the LSB
                    if frame.getpixel((x_byte * 8 + bit, y)) >= 128:
                        value |= 1 << bit
                data.append(value)
    (RES / 'flames' / 'flames_bw.bin').write_bytes(data)


def save_menu_icons():
    out = RES / 'images'
    out.mkdir(parents=True, exist_ok=True)
    color = Image.open(SRC / 'menu_color.png').convert('RGB').resize((25, 25), Image.Resampling.LANCZOS)
    color.save(out / 'menu~color.png')
    bw = Image.open(SRC / 'menu_bw.png').convert('L').resize((25, 25), Image.Resampling.LANCZOS)
    bw.point(lambda v: 255 if v >= 128 else 0).convert('1').save(out / 'menu.png')


if __name__ == '__main__':
    (RES / 'flames').mkdir(parents=True, exist_ok=True)
    save_digits()
    for name, (size, width, height) in COLOR_FLAMES.items():
        save_apng(name, size, width, height)
    save_bw_flames()
    save_menu_icons()
    for path in sorted((RES / 'flames').iterdir()):
        print(path.name, path.stat().st_size)
