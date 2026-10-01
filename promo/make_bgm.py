# -*- coding: utf-8 -*-
"""
为 ImageMatrix 介绍视频合成 40 秒轻松明快 BGM（纯 numpy 合成，无外部素材/版权问题）。

风格：C 大调 · 120 BPM · 清脆铃音(celesta)旋律 + 拨弦琶音 + 柔和铺底 + 轻打击乐。
结构随视频 7 幕的转场对齐（0/4/12/20/24/30/36s 为段落边界），结尾和弦收束并渐弱。

用法:
  python make_bgm.py            # 生成 bgm.wav（40s，44.1kHz 立体声）
"""
import numpy as np

SR = 44100
DUR = 40.0
BPM = 120.0
BEAT = 60.0 / BPM          # 0.5s
BAR = 4 * BEAT             # 2s
N = int(SR * DUR)

L = np.zeros(N, np.float64)
R = np.zeros(N, np.float64)


def midi_hz(m):
    return 440.0 * 2 ** ((m - 69) / 12)


# 音名速记（C4=60）
C4, D4, E4, F4, G4, A4, B4 = 60, 62, 64, 65, 67, 69, 71
C5, D5, E5, F5, G5, A5, B5 = C4 + 12, D4 + 12, E4 + 12, F4 + 12, G4 + 12, A4 + 12, B4 + 12
C6, D6, E6, F6, G6 = C5 + 12, D5 + 12, E5 + 12, F5 + 12, G5 + 12


def place(sig, t0, gain_l=1.0, gain_r=1.0):
    """把单声道片段写进立体声缓冲。"""
    i0 = int(t0 * SR)
    if i0 >= N:
        return
    seg = sig[: N - i0]
    L[i0:i0 + len(seg)] += seg * gain_l
    R[i0:i0 + len(seg)] += seg * gain_r


def bell(m, t0, dur=0.6, amp=0.13, pan=0.35):
    """清脆铃音/钢片琴：基频 + 高次泛音，快速起音、缓慢衰减。"""
    f = midi_hz(m)
    n = int(SR * min(dur + 1.4, DUR - t0))
    if n <= 0:
        return
    t = np.arange(n) / SR
    k = min(4.0, 0.4 / dur + 2.5)          # 越短促衰减越快
    sig = (np.sin(2 * np.pi * f * t) * np.exp(-k * t)
           + 0.45 * np.sin(2 * np.pi * f * 2 * t) * np.exp(-k * 1.5 * t)
           + 0.16 * np.sin(2 * np.pi * f * 4 * t) * np.exp(-k * 2.0 * t))
    sig *= amp
    gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    place(sig, t0, gl * 1.4, gr * 1.4)


def pluck(m, t0, amp=0.10, pan=-0.4):
    """拨弦琶音：正弦 + 二次谐波，快速指数衰减。"""
    f = midi_hz(m)
    n = int(SR * 0.6)
    t = np.arange(n) / SR
    sig = (np.sin(2 * np.pi * f * t) + 0.25 * np.sin(2 * np.pi * f * 2 * t)) \
        * np.exp(-t * 9.0) * amp
    gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    place(sig, t0, gl * 1.4, gr * 1.4)


def bass(m, t0, amp=0.17):
    """低音：正弦 + 少量二次谐波，短促。"""
    f = midi_hz(m) / 2
    n = int(SR * 0.5)
    t = np.arange(n) / SR
    env = np.minimum(t / 0.012, 1.0) * np.exp(-t * 5.0)
    place((np.sin(2 * np.pi * f * t) + 0.12 * np.sin(2 * np.pi * f * 2 * t))
          * env * amp, t0)


def pad_chord(ms, t0, dur, amp=0.05):
    """铺底和弦：轻微失谐正弦，慢起慢收。"""
    n = int(SR * (dur + 0.1))
    t = np.arange(n) / SR
    env = np.minimum(t / 0.4, 1.0) * np.minimum((dur + 0.1 - t) / 0.4, 1.0)
    sig = np.zeros(n)
    for m in ms:
        f = midi_hz(m)
        sig += (np.sin(2 * np.pi * f * t)
                + 0.6 * np.sin(2 * np.pi * f * 1.004 * t)
                + 0.6 * np.sin(2 * np.pi * f * 0.996 * t))
    sig *= amp * env / len(ms)
    place(sig, t0, 1.0, 0.92)     # 左右略差，展宽


def shaker(t0, amp=0.032, pan=0.25):
    n = int(SR * 0.09)
    t = np.arange(n) / SR
    noise = np.random.default_rng(int(t0 * 997)).standard_normal(n)
    noise = np.diff(noise, prepend=0)          # 高通
    sig = noise * np.exp(-t * 55) * amp * 0.12
    gl, gr = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    place(sig, t0, gl * 1.4, gr * 1.4)


def kick(t0, amp=0.20):
    n = int(SR * 0.16)
    t = np.arange(n) / SR
    f = 105 * np.exp(-t * 16) + 42
    ph = 2 * np.pi * np.cumsum(f) / SR
    place(np.sin(ph) * np.exp(-t * 22) * amp, t0)


def tick(t0, amp=0.03):
    n = int(SR * 0.05)
    t = np.arange(n) / SR
    noise = np.random.default_rng(int(t0 * 131)).standard_normal(n)
    sig = np.diff(noise, prepend=0) * np.exp(-t * 110) * amp * 0.5
    place(sig, t0, 0.8, 1.2)


# ---------------- 和弦进行（每小节一个） ----------------
# midi: [根, 三, 五]（用于琶音/铺底），低音用根音
CH = {
    'C':  [C4, E4, G4], 'Am': [A4 - 12 + 12, C4 + 12, E4 + 12],
    'F':  [F4 - 12 + 12, A4, C5], 'G': [G4 - 12 + 12, B4, D4 + 24 - 12],
    'Dm7': [D4, F4 + 12, C4 + 24 - 12], 'F/': [F4, A4, C4 + 24 - 12],
}
CH = {
    'C':   [48, 52, 55],
    'Am':  [45, 52, 57],
    'F':   [41, 48, 53],
    'G':   [43, 50, 55],
    'Dm7': [38, 48, 53],
}
PAD = {  # 铺底用中音区
    'C': [60, 64, 67], 'Am': [57, 60, 64], 'F': [53, 57, 60],
    'G': [55, 59, 62], 'Dm7': [50, 53, 60],
}

# 段落 -> (起始小节, 小节数, 和弦列表)
SECTIONS = [
    (0,  2, ['C', 'C']),                    # 开场 0-4    柔和引入
    (2,  4, ['C', 'Am', 'F', 'G']),         # 概念 4-12
    (6,  4, ['C', 'Am', 'Dm7', 'G']),       # 排布 12-20
    (10, 2, ['F', 'G']),                    # 容错 20-24
    (12, 3, ['C', 'F', 'G']),               # 界面 24-30
    (15, 3, ['Am', 'F', 'G']),              # 命令行 30-36
    (18, 2, ['F', 'C']),                    # 结尾 36-40
]

# ---------------- 铺底 + 低音 ----------------
for bar0, nbars, chords in SECTIONS:
    for i, ch in enumerate(chords):
        t0 = (bar0 + i) * BAR
        pad_chord(PAD[ch], t0, BAR)
        # 低音：第 1、3 拍（结尾段更疏）
        if t0 < 38.0 and not (bar0 == 0):
            bass(CH[ch][0], t0)
            bass(CH[ch][0], t0 + 2 * BEAT, amp=0.13)
        elif bar0 == 0:
            bass(CH[ch][0], t0, amp=0.12)

# ---------------- 琶音（8 分音符，进入主题段后） ----------------
def arp_pattern(ms, step):
    """上-下琶音：根+12, 五+12, 三+24, 五+12 循环。"""
    seq = [ms[0] + 12, ms[2] + 12, ms[1] + 24, ms[2] + 12]
    return seq[step % 4]

for bar0, nbars, chords in SECTIONS:
    if bar0 < 2:                     # 开场不铺琶音
        continue
    for i, ch in enumerate(chords):
        ms = CH[ch]
        t_bar = (bar0 + i) * BAR
        if t_bar >= 38.0:            # 结尾只留铃音收束
            continue
        for s in range(8):
            pluck(arp_pattern(ms, s), t_bar + s * BEAT / 2,
                  amp=0.10 if s % 2 == 0 else 0.075)

# ---------------- 轻打击乐 ----------------
for bar0, nbars, chords in SECTIONS:
    if bar0 < 2:
        continue
    for i in range(nbars):
        t_bar = (bar0 + i) * BAR
        if t_bar >= 38.0:
            continue
        kick(t_bar)
        kick(t_bar + 2 * BEAT, amp=0.15)
        tick(t_bar + 1 * BEAT)
        tick(t_bar + 3 * BEAT)
        for s in range(8):           # 16 分沙锤太密，用 8 分
            shaker(t_bar + s * BEAT / 2, amp=0.03 if s % 2 else 0.045)

# ---------------- 铃音旋律 ----------------
def add_melody(bar0, notes):
    """notes: [(小节内拍位, midi, 时值拍)]"""
    for beat_pos, m, dur in notes:
        bell(m, bar0 * BAR + beat_pos * BEAT, dur=dur * BEAT)

# 开场点缀（0-4s，Cmaj7）
add_melody(0, [(0.0, E5, 0.5), (1.5, G5, 0.5), (3.0, C6, 1.0)])
add_melody(1, [(0.0, D5, 0.5), (1.5, G5, 0.5), (3.0, B5, 1.0)])

# A 段（4-12s，C Am F G）
add_melody(2, [(0.0, E5, 0.5), (1.0, G5, 0.5), (2.0, C6, 1.0), (3.0, B5, 0.5)])
add_melody(3, [(0.0, A5, 0.5), (1.0, C6, 0.5), (2.0, E5, 1.0), (3.0, G5, 0.5)])
add_melody(4, [(0.0, F5, 0.5), (1.0, A5, 0.5), (2.0, C6, 1.0), (3.0, D6, 0.5)])
add_melody(5, [(0.0, B5, 0.5), (1.0, D6, 0.5), (2.0, G5, 1.0), (3.0, D5, 0.5)])

# B 段（12-20s，C Am Dm7 G）
add_melody(6, [(0.0, G5, 0.5), (0.5, E5, 0.5), (1.0, C5, 0.5), (2.0, E5, 1.0), (3.5, G5, 0.5)])
add_melody(7, [(0.0, A5, 0.5), (0.5, C6, 0.5), (1.0, E6, 0.5), (2.0, C6, 1.0), (3.0, B5, 0.5)])
add_melody(8, [(0.0, D6, 0.5), (1.0, A5, 0.5), (2.0, F5, 1.0), (3.0, A5, 0.5)])
add_melody(9, [(0.0, B5, 0.5), (1.0, G5, 0.5), (2.0, D5, 1.0), (3.0, D6, 0.5)])

# C 段（20-24s，F G，上行推进）
add_melody(10, [(0.0, C5, 0.5), (0.5, F5, 0.5), (1.0, A5, 0.5), (1.5, C6, 0.5), (2.0, F6, 1.5)])
add_melody(11, [(0.0, B5, 0.5), (0.5, D6, 0.5), (1.0, G6, 1.5), (3.0, D6, 0.5)])

# D 段（24-30s，C F G，明亮再现）
add_melody(12, [(0.0, E6, 0.5), (1.0, C6, 0.5), (2.0, G5, 1.0), (3.0, C6, 0.5)])
add_melody(13, [(0.0, A5, 0.5), (1.0, F6, 0.5), (2.0, C6, 1.0), (3.0, A5, 0.5)])
add_melody(14, [(0.0, B5, 0.5), (0.5, D6, 0.5), (1.0, G6, 1.0), (2.5, D6, 0.5), (3.0, B5, 0.5)])

# E 段（30-36s，Am F G）
add_melody(15, [(0.0, C6, 0.5), (0.5, E6, 0.5), (1.0, A5, 1.0), (2.0, E6, 0.5), (3.0, C6, 0.5)])
add_melody(16, [(0.0, A5, 0.5), (1.0, F6, 0.5), (1.5, C6, 0.5), (2.0, F6, 1.0), (3.0, G5, 0.5)])
add_melody(17, [(0.0, G5, 0.5), (0.5, B5, 0.5), (1.0, D6, 0.5), (1.5, G5, 0.5),
                (2.0, B5, 1.0), (3.5, A5, 0.5)])

# 结尾（36-40s，F -> C 收束）
add_melody(18, [(0.0, A5, 1.0), (1.5, G5, 1.0), (3.0, E5, 1.0)])
add_melody(19, [(0.0, C6, 4.0), (0.25, E6, 4.0), (0.5, G5, 4.0)])   # 终止和弦长铃

# ---------------- 段落音量包络 + 全局淡出 ----------------
times = np.array([0.0, 4.0, 12.0, 20.0, 24.0, 30.0, 36.0, 38.2, 39.2, 40.0])
gains = np.array([0.62, 0.82, 0.86, 0.93, 1.00, 1.00, 0.88, 0.88, 0.45, 0.0])
env = np.interp(np.arange(N) / SR, times, gains)
L *= env
R *= env

# 轻限幅 + 归一化
mix = np.stack([L, R], axis=1)
mix = np.tanh(mix * 1.4) / np.tanh(1.4)
mix /= np.abs(mix).max()
mix *= 0.88
pcm = (mix * 32767).astype(np.int16)

import wave
with wave.open('bgm.wav', 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(pcm.tobytes())

print('bgm.wav written: %.1fs, peak %.2f, rms %.3f'
      % (N / SR, np.abs(mix).max(), float(np.sqrt((mix ** 2).mean()))))
