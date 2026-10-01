# -*- coding: utf-8 -*-
"""
ImageMatrix 介绍视频渲染脚本（重写版，修复全部已知问题）
1920x1080 @ 60fps，清新简约风格，PIL 逐帧绘制（2x 超采样），ffmpeg 编码 H.264。

已修复：
  * text() 走精灵混合（PIL 直绘忽略墨水 alpha，会跳变/豆腐块）
  * 所有直绘形状预混墨水（blend 到已知底色，避免透明度被忽略）
  * 手绘 check_mark / swap_arrows（避免字体缺字形）
  * fade_sprite 用对象本身做键（避免 id 复用串缓存）
  * 场景 2 矩阵卡行距修正（不再溢出）
  * 场景 5 截图交叉淡化（不再空窗）
  * 场景 1 副标题去掉缺字形的 ⇄

用法:
  python render_video.py preview 1.6 7.5 15.5 ...   # 渲染关键帧到 preview/
  python render_video.py render                     # 全量渲染输出 MP4
"""
import math
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

# ---------------- 全局配置 ----------------
FPS = 60
DUR = 40.0
W1, H1 = 1920, 1080
SUP = 2                                   # 超采样倍数
W, H = W1 * SUP, H1 * SUP

ROOT = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(ROOT)
PREVIEW = os.path.join(ROOT, 'preview')
FFMPEG = os.path.join(os.environ.get('APPDATA', ''),
                      r'Python\Python314\site-packages\imageio_ffmpeg\binaries\ffmpeg-win-x86_64-v7.1.exe')
if not os.path.exists(FFMPEG):
    import imageio_ffmpeg
    FFMPEG = imageio_ffmpeg.get_ffmpeg_exe()

# ---------------- 配色（清新简约） ----------------
INK = (36, 52, 60)          # 主文字
SUB = (116, 136, 143)       # 次文字
TEAL = (42, 175, 162)       # 主题色
TEAL_D = (30, 140, 130)
BLUE = (86, 148, 214)
PEACH = (255, 150, 128)
CARD_LINE = (225, 237, 233)
DARKCARD = (38, 54, 62)     # 代码卡片底色
DARKTEXT = (223, 243, 238)
PAPER = (250, 252, 251)     # 背景近似底色（预混用）

FONT_DIR = r'C:\Windows\Fonts'
FONT_FILES = {
    'yahei': 'msyh.ttc',
    'yahei_bold': 'msyhbd.ttc',
    'yahei_light': 'msyhl.ttc',
    'mono': 'CascadiaMono.ttf',
    'mono_code': 'consola.ttf',
    'mono_bold': 'consolab.ttf',
}
_fonts = {}


def font(name, size):
    """size 为 1080p 视觉像素，内部乘超采样。"""
    key = (name, size)
    if key not in _fonts:
        _fonts[key] = ImageFont.truetype(
            os.path.join(FONT_DIR, FONT_FILES[name]), int(size * SUP))
    return _fonts[key]


# ---------------- 缓动函数 ----------------
def clamp01(x):
    return 0.0 if x < 0 else (1.0 if x > 1 else x)


def seg(t, a, b):
    """t 在 [a,b] 内的线性进度 0..1"""
    if b <= a:
        return 1.0 if t >= b else 0.0
    return clamp01((t - a) / (b - a))


def eoc(p):     # easeOutCubic
    p = clamp01(p)
    return 1 - (1 - p) ** 3


def eio(p):     # easeInOutCubic
    p = clamp01(p)
    if p < 0.5:
        return 4 * p ** 3
    return 1 - (-2 * p + 2) ** 3 / 2


def eob(p):     # easeOutBack（轻微回弹）
    p = clamp01(p)
    c1, c3 = 1.70158, 2.70158
    return 1 + c3 * (p - 1) ** 3 + c1 * (p - 1) ** 2


def fin(t, a, d=0.5):        # 淡入
    return eoc(seg(t, a, a + d))


def fout(t, b, d=0.35):      # 淡出
    return 1 - eoc(seg(t, b - d, b))


# ---------------- 基础绘制助手 ----------------
def A(alpha):
    return int(255 * clamp01(alpha))


def blend(base, ink, a):
    """把 ink 向 base 预混（1=本色）。PIL 直绘忽略墨水 alpha，必须预混。"""
    a = clamp01(a)
    return tuple(int(base[i] + (ink[i] - base[i]) * a) for i in range(3))


_shadow_cache = {}
_card_cache = {}


def shadow_for(w, h, r, blur, op):
    key = (w, h, r, blur, op)
    if key not in _shadow_cache:
        pad = blur * 2
        im = Image.new('RGBA', (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([pad, pad, pad + w, pad + h], radius=r,
                            fill=(30, 52, 60, A(op)))
        _shadow_cache[key] = im.filter(ImageFilter.GaussianBlur(blur))
    return _shadow_cache[key]


def _rgba(c):
    """统一成 4 元组颜色。"""
    if c is None:
        return None
    return tuple(c[:3]) + ((c[3],) if len(c) > 3 else (255,))


def card_sprite(w, h, r, fill, line, line_w=2):
    key = (w, h, r, fill, line, line_w)
    if key not in _card_cache:
        im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([line_w // 2, line_w // 2, w - line_w // 2, h - line_w // 2],
                            radius=r, fill=_rgba(fill),
                            outline=_rgba(line), width=line_w)
        _card_cache[key] = im
    return _card_cache[key]


def panel(work, x, y, w, h, r=28, fill=(255, 255, 255, 255), line=CARD_LINE,
          alpha=1.0, shadow_op=0.09, blur=30):
    """带软阴影的圆角卡片，x,y 为 1080p 视觉坐标。"""
    x, y, w, h = int(x * SUP), int(y * SUP), int(w * SUP), int(h * SUP)
    r, blur = r * SUP, blur * SUP
    if shadow_op > 0 and alpha > 0.02:
        sh = shadow_for(w, h, r, blur, shadow_op)
        pad = blur * 2
        work.alpha_composite(fade_sprite(sh, alpha), dest=(x - pad, y - pad))
    if alpha > 0.02:
        cs = card_sprite(w, h, r, tuple(fill[:3]) + (A(alpha),),
                         tuple(line[:3]) + (A(alpha),) if line else None)
        work.alpha_composite(cs, dest=(x, y))


_fade_cache = {}


def fade_sprite(spr, alpha):
    """返回按 alpha 缩放过 alpha 通道的精灵。

    Image 不可哈希（该 Pillow 版本），故用 id 做键 + 身份校验，防 id 复用串缓存。
    """
    if alpha >= 0.999:
        return spr
    a = round(alpha, 3)
    ent = _fade_cache.get(id(spr))
    if ent is not None and ent[0] is spr and ent[1] == a:
        return ent[2]
    im = spr.copy()
    ch = spr.getchannel('A').point(lambda v: int(v * alpha))
    im.putalpha(ch)
    if len(_fade_cache) > 400:
        _fade_cache.clear()
    _fade_cache[id(spr)] = (spr, a, im)
    return im


def blit(work, spr, x, y, alpha=1.0):
    if alpha <= 0.01:
        return
    work.alpha_composite(fade_sprite(spr, alpha), dest=(int(x * SUP), int(y * SUP)))


# ---------------- 文字（精灵混合：PIL 直绘忽略墨水 alpha） ----------------
_text_cache = {}


def text_sprite(s, fnt, fill, anchor):
    """渲染文字到紧贴的精灵。返回 (精灵, 视觉像素偏移 x, y)。"""
    key = (s, id(fnt), tuple(fill[:3]), anchor)
    ent = _text_cache.get(key)
    if ent is not None:
        return ent
    meas = ImageDraw.Draw(Image.new('RGBA', (8, 8)))
    x0, y0, x1, y1 = meas.textbbox((0, 0), s, font=fnt, anchor=anchor)
    pad = 6
    w = max(1, x1 - x0 + pad * 2)
    h = max(1, y1 - y0 + pad * 2)
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.text((pad - x0, pad - y0), s, font=fnt,
           fill=tuple(fill[:3]) + (255,), anchor=anchor)
    if len(_text_cache) > 600:
        _text_cache.clear()
    ent = (im, (x0 - pad) / SUP, (y0 - pad) / SUP)
    _text_cache[key] = ent
    return ent


def text(work, xy, s, fnt, fill, anchor='la', alpha=1.0):
    if alpha <= 0.01 or not s:
        return
    spr, ox, oy = text_sprite(s, fnt, fill, anchor)
    blit(work, spr, xy[0] + ox, xy[1] + oy, alpha)


def text_w(s, fnt):
    return ImageDraw.Draw(Image.new('RGB', (8, 8))).textlength(s, font=fnt) / SUP


def pill(work, cx, cy, s, fnt, fg, bg, alpha=1.0, dot=None, pad_x=30, h=64):
    """圆角小徽章，返回宽度。"""
    tw = text_w(s, fnt)
    w = tw + pad_x * 2 * (1 if dot is None else 1.35)
    x, y = cx - w / 2, cy - h / 2
    panel(work, x, y, w, h, r=h / 2, fill=bg, line=CARD_LINE, alpha=alpha,
          shadow_op=0.06, blur=18)
    tx = cx + (16 if dot else 0)
    if dot:
        d = ImageDraw.Draw(work)
        rr = 7 * SUP
        d.ellipse([(cx - w / 2 + pad_x) * SUP - rr, cy * SUP - rr,
                   (cx - w / 2 + pad_x) * SUP + rr, cy * SUP + rr],
                  fill=blend(tuple(bg[:3]), tuple(dot[:3]), alpha) + (255,))
    text(work, (tx, cy + 1), s, fnt, fg, anchor='mm', alpha=alpha)
    return w


def check_mark(work, x, y, size, color, alpha=1.0, width=6, base=None):
    """手绘对勾（避免字体缺字形）。base 为所在底色，用于预混墨水。"""
    if alpha <= 0.01:
        return
    d = ImageDraw.Draw(work)
    s = size * SUP
    pts = [(x * SUP - s * 0.55, y * SUP + s * 0.02),
           (x * SUP - s * 0.12, y * SUP + s * 0.42),
           (x * SUP + s * 0.58, y * SUP - s * 0.42)]
    col = blend(base, color, alpha) if base else tuple(color[:3])
    d.line(pts, fill=col + (255,), width=max(2, int(width * SUP)), joint='curve')


def swap_arrows(work, cx, cy, size, color, alpha=1.0, base=None):
    """双向箭头（手绘，避免字体缺字形 ⇄）。base 为所在底色。"""
    if alpha <= 0.01:
        return
    d = ImageDraw.Draw(work)
    s = size * SUP
    lw = max(2, int(0.10 * s))
    col = blend(base, color, alpha) if base else tuple(color[:3])
    y1, y2 = cy * SUP - s * 0.28, cy * SUP + s * 0.28
    ah = s * 0.30
    # 上箭头 →
    d.line([(cx * SUP - s * 0.5, y1), (cx * SUP + s * 0.42, y1)], fill=col, width=lw)
    d.polygon([(cx * SUP + s * 0.5, y1), (cx * SUP + s * 0.5 - ah, y1 - ah * 0.55),
               (cx * SUP + s * 0.5 - ah, y1 + ah * 0.55)], fill=col)
    # 下箭头 ←
    d.line([(cx * SUP + s * 0.5, y2), (cx * SUP - s * 0.42, y2)], fill=col, width=lw)
    d.polygon([(cx * SUP - s * 0.5, y2), (cx * SUP - s * 0.5 + ah, y2 - ah * 0.55),
               (cx * SUP - s * 0.5 + ah, y2 + ah * 0.55)], fill=col)


# ---------------- 背景（两种形态缓慢互溶） ----------------
def make_bg(shift=0.0):
    base = Image.new('RGBA', (W, H), (250, 252, 251, 255))
    blobs = [
        (0.86, 0.16, 640, (62, 198, 184), 0.10),   # 右上 青绿
        (0.10, 0.42, 560, (111, 177, 232), 0.09),  # 左中 天蓝
        (0.80, 0.88, 660, (255, 179, 138), 0.10),  # 右下 蜜桃
        (0.22, 0.02, 420, (184, 192, 240), 0.08),  # 左上 淡紫
    ]
    import numpy as np
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    for fx, fy, r, col, amax in blobs:
        cx = (fx + (shift * 0.02 if fx > 0.5 else -shift * 0.02)) * W
        cy = (fy + shift * 0.015 * (1 if fy > 0.5 else -1)) * H
        rr = r * SUP
        dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
        a = np.clip(1 - dist / rr, 0, 1) ** 2.2 * amax * 255
        layer = Image.fromarray(np.dstack([
            np.full((H, W), col[0], np.uint8),
            np.full((H, W), col[1], np.uint8),
            np.full((H, W), col[2], np.uint8),
            a.astype(np.uint8)]), 'RGBA')
        base.alpha_composite(layer)
    return base


# ---------------- 应用图标 ----------------
_icon_cache = {}


def icon_sprite(size_v):
    """4x4 马赛克图标，部分格子带 R/G/B/Y 字样，返回超采样精灵。"""
    size = int(size_v * SUP)
    if size in _icon_cache:
        return _icon_cache[size]
    n, gap = 4, max(2, size // 34)
    pad = size // 14
    cell = (size - gap * 3 - pad * 2) // n
    r = cell // 4
    cols = [(64, 201, 186), (96, 178, 226), (255, 176, 148), (126, 199, 180),
            (96, 178, 226), (255, 208, 152), (64, 201, 186), (172, 162, 226),
            (255, 176, 148), (64, 201, 186), (255, 208, 152), (96, 178, 226),
            (126, 199, 180), (172, 162, 226), (96, 178, 226), (64, 201, 186)]
    marks = {5: 'G', 10: 'B', 6: 'Y', 0: 'R'}
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(os.path.join(FONT_DIR, 'consolab.ttf'), int(cell * 0.62))
    for i in range(n):
        for j in range(n):
            idx = i * n + j
            x0 = pad + j * (cell + gap)
            y0 = pad + i * (cell + gap)
            c = cols[idx]
            d.rounded_rectangle([x0, y0, x0 + cell, y0 + cell], radius=r,
                                fill=c + (255,))
            if idx in marks:
                d.text((x0 + cell / 2, y0 + cell / 2 - cell * 0.02), marks[idx],
                       font=f, fill=(255, 255, 255, 235), anchor='mm')
    _icon_cache[size] = im
    return im


def draw_icon(work, cx, cy, size_v, alpha=1.0, scale=1.0, rot=0.0):
    spr = icon_sprite(size_v)
    if scale != 1.0:
        ns = (int(spr.width * scale), int(spr.height * scale))
        spr = spr.resize(ns, Image.LANCZOS)
    blit(work, spr, cx - spr.width / (2 * SUP), cy - spr.height / (2 * SUP), alpha)


# ---------------- 8x5 演示小图（场景 2） ----------------
DEMO_COLS, DEMO_ROWS = 8, 5


def demo_grid():
    """返回 5x8 的 (R,G,B) 网格 —— 迷你风景：天空/太阳/远山/草地。"""
    g = []
    for r in range(DEMO_ROWS):
        row = []
        for c in range(DEMO_COLS):
            t = c / (DEMO_COLS - 1)
            if r == 0 and c in (4, 5):
                col = (255, 208, 130) if r == 0 else (255, 222, 160)
            elif r == 1 and c in (3, 4, 5):
                col = (255, 196, 112) if c == 4 else (255, 214, 138)
            elif r == 2:
                col = (150, 212, 190) if c < 2 else (206, 238, 226)
            elif r == 3:
                col = (108, 190, 162) if c < 2 else ((168, 220, 198) if c < 4 else (214, 240, 230))
            else:
                col = (96, 182, 152) if c < 4 else ((150, 212, 190) if c < 6 else (222, 242, 234))
            # 轻微横向渐变，让数值自然变化
            k = 1 - 0.10 * t
            row.append(tuple(int(v * k) for v in col))
        g.append(row)
    return g


def gray_of(rgb):
    # 与程序一致的整数灰度公式
    return (299 * rgb[0] + 587 * rgb[1] + 114 * rgb[2] + 500) // 1000


# ---------------- 截图精灵 ----------------
_shot_cache = {}


def shot_sprite(path, disp_w, radius=16):
    key = (path, disp_w, radius)
    if key in _shot_cache:
        return _shot_cache[key]
    im = Image.open(path).convert('RGBA')
    scale = disp_w * SUP / im.width
    im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
    r = radius * SUP
    mask = Image.new('L', im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, im.width - 1, im.height - 1],
                                           radius=r, fill=255)
    im.putalpha(mask)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, im.width - 1, im.height - 1], radius=r,
                        outline=(205, 222, 216, 255), width=2 * SUP)
    _shot_cache[key] = im
    return im


# ================================================================
#  场景
# ================================================================
SCENES = []


def scene(t0, t1):
    def deco(fn):
        SCENES.append((t0, t1, fn))
        return fn
    return deco


def headline(work, t, s, y=148, size=64, alpha=1.0, rise=34):
    """统一的大标题：淡入 + 上浮。"""
    a = alpha * fin(t, 0.0, 0.55)
    dy = (1 - eoc(seg(t, 0.0, 0.55))) * rise
    text(work, (960, y + dy), s, font('yahei_bold', size), INK, anchor='mm', alpha=a)


# ---------- 场景 1：开场 ----------
@scene(0.0, 4.2)
def sc_open(work, d, t):
    lt = t - 0.0
    out = fout(t, 4.2, 0.3)
    a_icon = eob(seg(lt, 0.15, 0.95))
    draw_icon(work, 960, 386, 196, alpha=out * clamp01(a_icon), scale=0.6 + 0.4 * a_icon)
    a1 = out * fin(lt, 0.55, 0.7)
    dy1 = (1 - eoc(seg(lt, 0.55, 1.25))) * 36
    text(work, (960, 596 + dy1), 'ImageMatrix', font('yahei_bold', 140), INK,
         anchor='mm', alpha=a1)
    a2 = out * fin(lt, 0.95, 0.7)
    dy2 = (1 - eoc(seg(lt, 0.95, 1.65))) * 30
    text(work, (960, 716 + dy2), '图片与数值矩阵  双向转换工具', font('yahei_light', 52),
         SUB, anchor='mm', alpha=a2)
    a3 = out * fin(lt, 1.35, 0.7)
    w_line = 260 * eoc(seg(lt, 1.35, 2.0))
    d.line([(960 - w_line / 2) * SUP, 800 * SUP, (960 + w_line / 2) * SUP, 800 * SUP],
           fill=blend(PAPER, TEAL, a3) + (255,), width=3 * SUP)


# ---------- 场景 2：核心概念（图片 ⇄ 矩阵） ----------
GRID = demo_grid()
GRAYS = [[gray_of(GRID[r][c]) for c in range(DEMO_COLS)] for r in range(DEMO_ROWS)]

# 数字飞行粒子（阶段 A：图片 → 矩阵）
_PARTS = []
for _r in range(DEMO_ROWS):
    for _c in range(DEMO_COLS):
        _PARTS.append((_r, _c, 5.55 + (_r * DEMO_COLS + _c) * 0.011))

CELL = 52          # 小图单元格视觉尺寸
GAP = 5
MOSA_W = DEMO_COLS * CELL + (DEMO_COLS - 1) * GAP
MOSA_H = DEMO_ROWS * CELL + (DEMO_ROWS - 1) * GAP
MOSA_X, MOSA_Y = 268, 392          # 马赛克左上（卡片内）
PX, PY = 208, 330                  # 图片卡片位置
MX, MY = 1036, 316                 # 矩阵卡片位置
MAT_LH = 46                        # 矩阵行距（修正后）
MAT_X0, MAT_Y0 = 46, 100           # 矩阵文本区内边距/起始


def _cell_xy(r, c):
    return MOSA_X + c * (CELL + GAP), MOSA_Y + r * (CELL + GAP)


def _draw_mosaic(work, d, t, fill_p, alpha=1.0):
    """画 8x5 马赛克。fill_p(r,c) 返回该格上色进度 0..1（0=纸色，1=本色）"""
    paper = (246, 250, 249)
    for r in range(DEMO_ROWS):
        for c in range(DEMO_COLS):
            x, y = _cell_xy(r, c)
            p = eoc(fill_p(r, c))
            col = GRID[r][c]
            cur = tuple(int(paper[i] + (col[i] - paper[i]) * p) for i in range(3))
            # 向卡片白色预混墨水（直绘忽略 alpha）
            fin_ = tuple(int(255 + (cur[i] - 255) * clamp01(alpha)) for i in range(3))
            rr = 10 * SUP
            d.rounded_rectangle([x * SUP, y * SUP, (x + CELL) * SUP, (y + CELL) * SUP],
                                radius=rr, fill=fin_ + (255,))


@scene(4.2, 12.4)
def sc_concept(work, d, t):
    lt = t - 4.2
    out = fout(t, 12.4, 0.3)
    # 标题
    a_h = out * fin(lt, 0.15, 0.55)
    dy_h = (1 - eoc(seg(lt, 0.15, 0.7))) * 30
    text(work, (960, 150 + dy_h), '一张图片，本质上就是一组数字',
         font('yahei_bold', 62), INK, anchor='mm', alpha=a_h)

    phaseA = fin(lt, 1.0, 0.5)          # 阶段 A 总体可见度
    phaseB = seg(lt, 4.9, 5.5)          # 进入还原阶段

    # ---- 左：图片卡片 ----
    pa = out * fin(lt, 0.45, 0.6)
    pdy = (1 - eoc(seg(lt, 0.45, 1.05))) * 40
    panel(work, PX, PY + pdy, 560, 470, r=26, alpha=pa)
    if pa > 0.02:
        text(work, (PX + 30, PY + pdy + 26), 'photo.png', font('mono', 30), SUB,
             alpha=pa)
        text(work, (PX + 530, PY + pdy + 28), '8 × 5 像素', font('yahei', 26), SUB,
             anchor='ra', alpha=pa)

        def fp(r, c):
            appear = seg(lt, 0.9 + (r * DEMO_COLS + c) * 0.012, 0.45)
            return eoc(appear)
        _draw_mosaic(work, d, t, fp, alpha=pa)

    # ---- 右：矩阵卡片（深色） ----
    ma = out * fin(lt, 0.75, 0.6)
    mdy = (1 - eoc(seg(lt, 0.75, 1.35))) * 40
    mx, my, mw, mh = MX, MY + mdy, 640, 470
    if ma > 0.02:
        # 阴影 + 深色圆角卡
        sh = shadow_for(int(mw * SUP), int(mh * SUP), 26 * SUP, 30 * SUP, 0.12)
        pad = 30 * SUP * 2
        work.alpha_composite(fade_sprite(sh, ma), dest=(int(mx * SUP) - pad, int(my * SUP) - pad))
        cs = Image.new('RGBA', (int(mw * SUP), int(mh * SUP)), (0, 0, 0, 0))
        cd = ImageDraw.Draw(cs)
        cd.rounded_rectangle([0, 0, mw * SUP - 1, mh * SUP - 1], radius=26 * SUP,
                             fill=DARKCARD + (A(ma),))
        work.alpha_composite(cs, dest=(int(mx * SUP), int(my * SUP)))
        # 三个窗口圆点（画在卡片精灵上，随卡片一起淡入）
        for i in range(3):
            rr = 7 * SUP
            cx0 = 34 + i * 30
            cd.ellipse([cx0 * SUP - rr, 40 * SUP - rr, cx0 * SUP + rr, 40 * SUP + rr],
                       fill=(96, 118, 126, 255))
        text(work, (mx + mw - 30, my + 42), 'photo.txt · GRAY', font('mono', 28),
             (150, 176, 184), anchor='ra', alpha=ma)
        # 矩阵数字：逐行打出（行距 46，8 行全部在卡片内）
        rows = ['# format GRAY', '# width 8   height 5', '']
        for r in range(DEMO_ROWS):
            rows.append(' '.join(f'{v:3d}' for v in GRAYS[r]))
        fmono = font('mono', 28)
        fh = font('mono', 24)
        for i, row in enumerate(rows):
            ty = my + MAT_Y0 + i * MAT_LH
            t0r = 1.35 + i * 0.22
            p = eoc(seg(lt, t0r, t0r + 0.5))
            if p <= 0:
                continue
            s = row[:max(0, int(len(row) * p * 1.15))]
            fnt = fh if i < 2 else fmono
            col = (128, 152, 160) if i < 2 else (223, 243, 238)
            text(work, (mx + MAT_X0, ty), s, fnt, col, alpha=ma * p)

    # ---- 中间双向箭头徽章 ----
    ba = out * fin(lt, 1.5, 0.5)
    pulse = 1 + 0.05 * math.sin(t * 3.2)
    if ba > 0.02:
        cxx, cyy = 800, 560
        rr = 64 * SUP * pulse
        d.ellipse([cxx * SUP - rr, cyy * SUP - rr, cxx * SUP + rr, cyy * SUP + rr],
                  fill=blend(PAPER, (255, 255, 255), ba) + (255,),
                  outline=blend(PAPER, TEAL, ba) + (255,), width=4 * SUP)
        swap_arrows(work, cxx, cyy, 52, TEAL_D, ba, base=(255, 255, 255))

    # ---- 飞行数字 ----
    tile_cache = {}
    cch = text_w('0', font('mono', 28))
    for r, c, t0p in _PARTS:
        if lt < 0.9 or phaseB >= 1.0:
            continue
        dur = 0.85
        p = seg(lt, t0p, t0p + dur)
        if 0 < p < 1:
            x0, y0 = _cell_xy(r, c)
            x0 += CELL / 2
            y0 += CELL / 2
            tx = mx + MAT_X0 + (c * 4 + 1.5) * cch
            ty = my + MAT_Y0 + (3 + r) * MAT_LH + 14
            bx = x0 + (tx - x0) * 0.45
            by = min(y0, ty) - 120
            u = eio(p)
            px_ = (1 - u) ** 2 * x0 + 2 * (1 - u) * u * bx + u ** 2 * tx
            py_ = (1 - u) ** 2 * y0 + 2 * (1 - u) * u * by + u ** 2 * ty
            al = math.sin(p * math.pi) ** 0.6
            key = str(GRAYS[r][c])
            if key not in tile_cache:
                ts = Image.new('RGBA', (64 * SUP, 44 * SUP), (0, 0, 0, 0))
                td = ImageDraw.Draw(ts)
                td.rounded_rectangle([0, 0, 64 * SUP, 44 * SUP], radius=10 * SUP,
                                     fill=(255, 255, 255, 235),
                                     outline=TEAL + (255,), width=2 * SUP)
                td.text((32 * SUP, 22 * SUP), key, font=font('mono_bold', 26),
                        fill=TEAL_D + (255,), anchor='mm')
                tile_cache[key] = ts
            blit(work, tile_cache[key], px_ - 32, py_ - 22, al * out)

    # ---- 阶段 B：还原 ----
    if phaseB > 0:
        a_ok = out * fin(lt, 5.6, 0.45)
        if a_ok > 0.02:
            pill(work, PX + 280, PY + 470 + 44, '从 TXT 还原 · 逐像素一致',
                 font('yahei', 30), (255, 255, 255), TEAL + (255,), alpha=a_ok, h=58)
        # 重新强调彩色格子（轻微闪光扫过）
        sweep = seg(lt, 5.0, 6.4)
        if 0 < sweep < 1:
            sx = MOSA_X - 30 + sweep * (MOSA_W + 120)
            grad = Image.new('RGBA', (60 * SUP, MOSA_H * SUP), (0, 0, 0, 0))
            gd = ImageDraw.Draw(grad)
            for i in range(12):
                aa = int(70 * (1 - abs(i - 6) / 6))
                gd.rectangle([i * 5 * SUP, 0, (i + 1) * 5 * SUP, MOSA_H * SUP],
                             fill=(255, 255, 255, aa))
            blit(work, grad, sx, MOSA_Y, 0.9 * out)

    # ---- 底部说明 ----
    a_c = out * fin(lt, 6.0, 0.55)
    if a_c > 0.02:
        text(work, (960, 906), '导出为 TXT 矩阵，再从 TXT 原样还原',
             font('yahei', 40), SUB, anchor='mm', alpha=a_c)


# ---------- 场景 3：5 种矩阵排布 ----------
FMTS = [
    ('CHANNELS', 'R / G / B / 灰度', '四个独立矩阵，信息最全', (64, 201, 186)),
    ('RGB', '每行一个像素', 'R, G, B 三元组', (86, 148, 214)),
    ('RGBA', '含透明通道', 'R, G, B, A 四元组', (172, 162, 226)),
    ('GRAY', '纯灰度矩阵', '只要亮度，体积最小', (116, 136, 143)),
    ('HEX', '十六进制颜色', '一行一个 RRGGBB', (255, 150, 128)),
]


@scene(12.4, 18.8)
def sc_formats(work, d, t):
    lt = t - 12.4
    out = fout(t, 18.8, 0.3)
    a_h = out * fin(lt, 0.1, 0.55)
    dy_h = (1 - eoc(seg(lt, 0.1, 0.65))) * 30
    text(work, (960, 150 + dy_h), '5 种矩阵排布，按需选择',
         font('yahei_bold', 62), INK, anchor='mm', alpha=a_h)

    cw, ch, gap = 320, 400, 34
    total = 5 * cw + 4 * gap
    x0 = (1920 - total) / 2
    y0 = 300
    for i, (name, l1, l2, col) in enumerate(FMTS):
        ta = 0.35 + i * 0.28
        a = out * eob(seg(lt, ta, ta + 0.55))
        if a <= 0.02:
            continue
        dy = (1 - eoc(seg(lt, ta, ta + 0.55))) * 56
        x = x0 + i * (cw + gap)
        panel(work, x, y0 + dy, cw, ch, r=30, alpha=min(1.0, a))
        # 顶部小色块图标：迷你 3x3 矩阵（向卡片白色预混）
        ix, iy = x + cw / 2, y0 + dy + 78
        cs, cg = 26, 5
        ox = ix - (3 * cs + 2 * cg) / 2
        aa = min(1.0, a)
        for rr_ in range(3):
            for cc_ in range(3):
                v = 0.55 + 0.45 * ((rr_ * 3 + cc_) % 5) / 4
                c3 = blend((255, 255, 255), tuple(int(v * ch_) for ch_ in col), aa)
                d.rounded_rectangle(
                    [(ox + cc_ * (cs + cg)) * SUP, (iy - 1.5 * (cs + cg) + rr_ * (cs + cg)) * SUP,
                     (ox + cc_ * (cs + cg) + cs) * SUP, (iy - 1.5 * (cs + cg) + rr_ * (cs + cg) + cs) * SUP],
                    radius=7 * SUP, fill=c3 + (255,))
        text(work, (x + cw / 2, y0 + dy + 208), name, font('mono_bold', 38), INK,
             anchor='mm', alpha=aa)
        text(work, (x + cw / 2, y0 + dy + 272), l1, font('yahei', 30), INK,
             anchor='mm', alpha=aa)
        text(work, (x + cw / 2, y0 + dy + 322), l2, font('yahei', 27), SUB,
             anchor='mm', alpha=aa * 0.95)

    a_n = out * fin(lt, 2.3, 0.5)
    if a_n > 0.02:
        text(work, (960, 880), '同一张图，下拉切换，随意导出', font('yahei', 38), SUB,
             anchor='mm', alpha=a_n)


# ---------- 场景 4：容错读取 ----------
MESSY = [
    '10, 20, 30',
    '(14 186 168)',
    '0.05 | 0.73 | 0.66',
    '12; 34; 56',
]
TOLERANT = [
    '逗号 / 空格 / 括号，都能当分隔符',
    '0–1 归一化小数，自动识别',
    '缺注释头，自动推断宽高',
    '数据多则截断，少则补零',
]


@scene(18.8, 23.8)
def sc_tolerant(work, d, t):
    lt = t - 18.8
    out = fout(t, 23.8, 0.3)
    a_h = out * fin(lt, 0.1, 0.55)
    dy_h = (1 - eoc(seg(lt, 0.1, 0.65))) * 30
    text(work, (960, 150 + dy_h), '读入足够宽容', font('yahei_bold', 62), INK,
         anchor='mm', alpha=a_h)

    # 左卡：随手写的矩阵
    la = out * fin(lt, 0.4, 0.6)
    ldy = (1 - eoc(seg(lt, 0.4, 1.0))) * 40
    panel(work, 190, 300 + ldy, 720, 480, r=30, alpha=la)
    text(work, (230, 344 + ldy), '随手写的 TXT 也能读', font('yahei', 32), SUB,
         alpha=la)
    fmono = font('mono', 40)
    for i, sline in enumerate(MESSY):
        ta = 0.9 + i * 0.4
        p = eoc(seg(lt, ta, ta + 0.45))
        if p <= 0:
            continue
        show = sline[:int(len(sline) * p * 1.2)]
        col = (86, 148, 214) if i == 2 else INK
        text(work, (230, 430 + i * 82 + ldy), show, fmono, col, alpha=la * p)

    # 右卡：自动处理
    ra = out * fin(lt, 0.65, 0.6)
    rdy = (1 - eoc(seg(lt, 0.65, 1.25))) * 40
    panel(work, 1010, 300 + rdy, 720, 480, r=30, alpha=ra)
    text(work, (1050, 344 + rdy), '剩下的交给程序', font('yahei', 32), SUB, alpha=ra)
    for i, sline in enumerate(TOLERANT):
        ta = 1.2 + i * 0.42
        p = eob(seg(lt, ta, ta + 0.5))
        if p <= 0.02:
            continue
        yy = 442 + i * 82 + rdy
        # 对勾圆片（向卡片白色预混）
        cxx, cyy = 1096, yy + 18
        pq = clamp01(la * min(1.0, p * 1.3))
        rr = 22 * SUP * min(1.0, p)
        d.ellipse([cxx * SUP - rr, cyy * SUP - rr, cxx * SUP + rr, cyy * SUP + rr],
                  fill=blend((255, 255, 255), (64, 201, 186), pq) + (255,))
        check_mark(work, cxx, cyy, 26, (255, 255, 255),
                   la * min(1, p * 1.4), 5, base=(64, 201, 186))
        text(work, (1146, yy + 20), sline, font('yahei', 34), INK,
             alpha=la * min(1, p * 1.5))


# ---------- 场景 5：真实界面 ----------
@scene(23.8, 30.2)
def sc_gui(work, d, t):
    lt = t - 23.8
    out = fout(t, 30.2, 0.3)
    a_h = out * fin(lt, 0.1, 0.5)
    text(work, (960, 132), '原生界面 · 简单直接', font('yahei_bold', 62), INK,
         anchor='mm', alpha=a_h)

    # 截图交叉淡化：下一张在上一张淡出前就开始淡入，避免空窗
    shots = [
        (os.path.join(PROJ, 'docs', 'screenshots', 'g3_pattern_loaded.png'), 1060,
         '打开图片，滚轮缩放、左键平移', 0.35, 2.55),
        (os.path.join(PROJ, 'docs', 'screenshots', 'g5_matrix_text.png'), 900,
         '窗口内直接预览矩阵文本，一键复制导出', 2.2, 4.6),
        (os.path.join(PROJ, 'docs', 'screenshots', 'g4_zoomed_grid.png'), 1060,
         '放大 8 倍以上，显示像素网格；状态栏实时取值', 4.35, 6.4),
    ]
    for path, dw, cap, ta, tb in shots:
        a = out * fin(lt, ta, 0.45) * fout(lt, tb, 0.35)
        if a <= 0.02:
            continue
        spr = shot_sprite(path, dw)
        dy = (1 - eoc(seg(lt, ta, ta + 0.45))) * 46
        x = (1920 - dw) / 2
        y = 210 + dy
        panel(work, x - 14, y - 14, dw + 28, spr.height / SUP + 28, r=24,
              fill=(255, 255, 255, 255), alpha=a, shadow_op=0.10)
        blit(work, spr, x, y, a)
        text(work, (960, y + spr.height / SUP + 66), cap, font('yahei', 36), SUB,
             anchor='mm', alpha=a)


# ---------- 场景 6：命令行 ----------
CLI_LINES = [
    ('PS> ', 'imagematrix-cli to-txt   -i photo.png -o photo.txt -f CHANNELS', (120, 200, 190), (223, 243, 238)),
    ('PS> ', 'imagematrix-cli to-image -i photo.txt -o back.png', (120, 200, 190), (223, 243, 238)),
    ('PS> ', 'imagematrix-cli gen    -o pattern.png -w 320 -h 240', (120, 200, 190), (223, 243, 238)),
    ('PS> ', 'imagematrix-cli selftest', (120, 200, 190), (223, 243, 238)),
]


@scene(30.2, 35.6)
def sc_cli(work, d, t):
    lt = t - 30.2
    out = fout(t, 35.6, 0.3)
    a_h = out * fin(lt, 0.1, 0.5)
    dy_h = (1 - eoc(seg(lt, 0.1, 0.6))) * 30
    text(work, (960, 150 + dy_h), '命令行，同样好用', font('yahei_bold', 62), INK,
         anchor='mm', alpha=a_h)

    ca = out * fin(lt, 0.35, 0.55)
    cdy = (1 - eoc(seg(lt, 0.35, 0.9))) * 40
    mx, my, mw, mh = 340, 268 + cdy, 1240, 500
    if ca > 0.02:
        sh = shadow_for(int(mw * SUP), int(mh * SUP), 26 * SUP, 30 * SUP, 0.13)
        pad = 30 * SUP * 2
        work.alpha_composite(fade_sprite(sh, ca), dest=(int(mx * SUP) - pad, int(my * SUP) - pad))
        cs = Image.new('RGBA', (int(mw * SUP), int(mh * SUP)), (0, 0, 0, 0))
        cd = ImageDraw.Draw(cs)
        cd.rounded_rectangle([0, 0, mw * SUP - 1, mh * SUP - 1], radius=26 * SUP,
                             fill=(34, 48, 56, A(ca)))
        work.alpha_composite(cs, dest=(int(mx * SUP), int(my * SUP)))
        for i in range(3):
            rr = 8 * SUP
            cx0 = 44 + i * 34
            cd.ellipse([cx0 * SUP - rr, 46 * SUP - rr, cx0 * SUP + rr, 46 * SUP + rr],
                       fill=(90, 112, 120, 255))
        text(work, (mx + mw - 44, my + 48), 'imagematrix-cli', font('mono', 26),
             (130, 156, 164), anchor='ra', alpha=ca)

        lh = 64
        f = font('mono', 29)
        for i, (pr, cmd, c1, c2) in enumerate(CLI_LINES):
            ta = 0.8 + i * 0.75
            p = seg(lt, ta, ta + len(cmd) * 0.012 + 0.15)
            if p <= 0:
                continue
            nch = int(len(cmd) * min(1.0, p * 1.12))
            yy = my + 120 + i * lh
            text(work, (mx + 60, yy), pr, f, c1, alpha=ca)
            text(work, (mx + 60 + text_w(pr, f), yy), cmd[:nch], f, c2, alpha=ca)
            # 光标（向深色卡片预混）
            if p < 1.0 and (t * 2.6) % 1 < 0.55:
                cx_ = mx + 60 + text_w(pr, f) + text_w(cmd[:nch], f)
                d.rectangle([cx_ * SUP + 4, (yy - 2) * SUP, cx_ * SUP + 4 + 3 * SUP,
                             (yy + 34) * SUP],
                            fill=blend((34, 48, 56), (223, 243, 238), ca) + (255,))
        # selftest 结果
        ta = 0.8 + 3 * 0.75 + 0.9
        pa = fin(lt, ta, 0.4)
        if pa > 0.02:
            yy = my + 120 + 4 * lh
            text(work, (mx + 60, yy), '[ok]', font('mono_bold', 29), (108, 220, 170),
                 alpha=ca * pa)
            text(work, (mx + 60 + text_w('[ok] ', f), yy), '20/20 tests passed',
                 font('mono', 29), (223, 243, 238), alpha=ca * pa)
            check_mark(work, mx + 60 + text_w('[ok] 20/20 tests passed ', f) + 26,
                       yy + 16, 26, (108, 220, 170), ca * pa, 5, base=(34, 48, 56))

    a_c = out * fin(lt, 3.6, 0.5)
    if a_c > 0.02:
        text(work, (960, 872), '批处理 · 脚本集成 · 20 项自检', font('yahei', 38),
             SUB, anchor='mm', alpha=a_c)


# ---------- 场景 7：结尾 ----------
@scene(35.6, 40.0)
def sc_end(work, d, t):
    lt = t - 35.6
    out = fout(t, 40.0, 0.55)
    a_icon = eob(seg(lt, 0.1, 0.85))
    draw_icon(work, 960, 340, 176, alpha=out * clamp01(a_icon),
              scale=0.6 + 0.4 * a_icon)
    a1 = out * fin(lt, 0.45, 0.6)
    text(work, (960, 540), 'ImageMatrix', font('yahei_bold', 118), INK, anchor='mm',
         alpha=a1)
    a2 = out * fin(lt, 0.75, 0.6)
    text(work, (960, 648), '把图片读成数字，把数字还原成图片', font('yahei_light', 46),
         SUB, anchor='mm', alpha=a2)

    chips = [('纯 C/C++', TEAL_D), ('零第三方依赖', BLUE), ('单文件 exe · 拷走即用', (232, 130, 110))]
    fnt = font('yahei', 32)
    ws = [text_w(s, fnt) + 96 for s, _ in chips]
    total = sum(ws) + 2 * 36
    x = (1920 - total) / 2
    for i, ((s, col), wch) in enumerate(zip(chips, ws)):
        ta = 1.1 + i * 0.22
        a = out * eob(seg(lt, ta, ta + 0.5))
        if a <= 0.02:
            continue
        cx = x + sum(ws[:i]) + i * 36 + wch / 2
        pill(work, cx, 768, s, fnt, (255, 255, 255), col + (255,), alpha=min(1, a),
             h=62)
    a3 = out * fin(lt, 1.7, 0.6)
    text(work, (960, 886), '纯 Win32 API · BMP 自实现编解码 · MIT', font('yahei', 28),
         (150, 168, 175), anchor='mm', alpha=a3 * 0.9)


# ================================================================
#  帧渲染
# ================================================================
BGS = None


def prep():
    global BGS
    BGS = (make_bg(0.0), make_bg(1.0))


def render_frame(t):
    wave = (1 - math.cos(2 * math.pi * t / 26)) / 2
    work = Image.blend(BGS[0], BGS[1], wave)
    for (t0, t1, fn) in SCENES:
        if t0 - 0.001 <= t < t1:
            fn(work, ImageDraw.Draw(work), t)
    out = work.resize((W1, H1), Image.LANCZOS).convert('RGB')
    return out


def do_preview(times):
    os.makedirs(PREVIEW, exist_ok=True)
    prep()
    for tt in times:
        fr = render_frame(tt)
        p = os.path.join(PREVIEW, 'key_%05d.png' % round(tt * 100))
        fr.save(p)
        print('saved', p)


def do_render():
    prep()
    os.makedirs(ROOT, exist_ok=True)
    out_path = os.path.join(ROOT, 'ImageMatrix_intro_1080p60.mp4')
    log = open(os.path.join(ROOT, 'ffmpeg.log'), 'wb')
    cmd = [FFMPEG, '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24',
           '-s', f'{W1}x{H1}', '-r', str(FPS), '-i', '-',
           '-c:v', 'libx264', '-preset', 'medium', '-crf', '17',
           '-pix_fmt', 'yuv420p', '-movflags', '+faststart', out_path]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL, stderr=log)
    n = int(DUR * FPS)
    import time as _time
    t_start = _time.time()
    for i in range(n):
        t = i / FPS
        frame = render_frame(t)
        proc.stdin.write(frame.tobytes())
        if i % 150 == 0:
            el = _time.time() - t_start
            eta = el / (i + 1) * (n - i - 1) if i else 0
            print(f'frame {i}/{n}  t={t:.2f}s  elapsed={el:.0f}s eta={eta:.0f}s',
                  flush=True)
    proc.stdin.close()
    proc.wait()
    log.close()
    print('done ->', out_path, 'rc=', proc.returncode)


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'preview':
        do_preview([float(x) for x in sys.argv[2:]] or [1.5])
    else:
        do_render()
