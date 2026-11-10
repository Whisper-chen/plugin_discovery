#!/usr/bin/env python3
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Generate the architecture SVGs in docs/images/.

    python3 -I docs/images/gen_diagrams.py            # write all SVGs
    python3 -I docs/images/gen_diagrams.py --check    # write, then check layout

No renderer is needed. --check estimates text width (DejaVu Sans metrics, roughly)
and reports text that overflows its box and boxes that overlap.
"""
import os
import sys
from xml.sax.saxutils import escape

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "DejaVu Sans, Helvetica, Arial, sans-serif"
MONO = "DejaVu Sans Mono, Menlo, Consolas, monospace"

# Colour roles, the same in every diagram.
C = {
    "consumer": ("#e8f5e9", "#43a047"),
    "iface":    ("#e3f2fd", "#1e88e5"),
    "core":     ("#ede7f6", "#5e35b1"),
    "backend":  ("#ffffff", "#546e7a"),
    "customer": ("#ffffff", "#2e7d32"),
    "vendor":   ("#eceff1", "#90a4ae"),
    "hw":       ("#cfd8dc", "#78909c"),
    "config":   ("#fff3e0", "#fb8c00"),
    "ok":       ("#e8f5e9", "#43a047"),
    "skip":     ("#fbe9e7", "#e53935"),
    "note":     ("#fffde7", "#fbc02d"),
    "group":    ("#fafafa", "#b0bec5"),
}
INK = "#263238"
MUTED = "#607d8b"


def text_width(s, size, mono=False):
    if mono:
        return len(s) * size * 0.60
    w = 0.0
    for ch in s:
        if ch in "il.,:;|'!()[] ":
            w += 0.32
        elif ch in "mwMW@":
            w += 0.92
        elif ch.isupper() or ch.isdigit():
            w += 0.68
        else:
            w += 0.56
    return w * size


class Svg:
    def __init__(self, name, w, h, title):
        self.name, self.w, self.h = name, w, h
        self.parts = []
        self.boxes = []   # (id, x, y, w, h, lines) for --check
        self.groups = []  # (id, x, y, w, h)
        self.loose = []   # free text: (x, y, s, size, anchor)
        self.arrows = []  # list of points
        self.title = title
        self._in_box = False

    def rect(self, x, y, w, h, role, rx=8, dashed=False, stroke_w=1.6):
        fill, stroke = C[role]
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" '
            f'stroke="{stroke}" stroke-width="{stroke_w}"{dash}/>')

    def text(self, x, y, s, size=13, weight="normal", anchor="middle", color=INK, mono=False,
             italic=False):
        if not self._in_box:
            self.loose.append((x, y, s, size, anchor, mono))
        style = ' font-style="italic"' if italic else ""
        fam = MONO if mono else FONT
        self.parts.append(
            f'<text x="{x}" y="{y}" font-family="{fam}" font-size="{size}" font-weight="{weight}" '
            f'fill="{color}" text-anchor="{anchor}"{style}>{escape(s)}</text>')

    def box(self, bid, x, y, w, h, role, title, lines=(), size=12, dashed=False, mono_lines=False,
            title_size=14, stroke_w=1.6):  # noqa: the title is bold, see check()
        """Box with a bold title and optional lines, all centred."""
        self.rect(x, y, w, h, role, dashed=dashed, stroke_w=stroke_w)
        self._in_box = True
        n = 1 + len(lines)
        line_h = size + 5
        block = title_size + (n - 1) * line_h
        cy = y + (h - block) / 2 + title_size - 2
        self.text(x + w / 2, cy, title, size=title_size, weight="bold")
        for i, ln in enumerate(lines):
            self.text(x + w / 2, cy + (i + 1) * line_h, ln, size=size, color=MUTED if not mono_lines else INK,
                      mono=mono_lines)
        self._in_box = False
        self.boxes.append((bid, x, y, w, h, [(title, title_size, False)] +
                           [(ln, size, mono_lines) for ln in lines]))

    def group(self, gid, x, y, w, h, label, role="group", dashed=False):
        """Container: label at the top-left. Not checked for overlap with its contents."""
        self.rect(x, y, w, h, role, rx=10, dashed=dashed)
        self.groups.append((gid, x, y, w, h))
        if label:
            self.text(x + 12, y + 20, label, size=13, weight="bold", anchor="start", color=MUTED)

    def arrow(self, pts, color=INK, dashed=False, width=1.6, label=None, label_at=None,
              label_anchor="middle", head=True):
        self.arrows.append(list(pts))
        d = " ".join(f"{'M' if i == 0 else 'L'}{x},{y}" for i, (x, y) in enumerate(pts))
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        mid = ' marker-end="url(#ah)"' if head else ""
        self.parts.append(
            f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}"{dash}{mid}/>')
        if label:
            lx, ly = label_at if label_at else ((pts[0][0] + pts[-1][0]) / 2, (pts[0][1] + pts[-1][1]) / 2 - 6)
            self.text(lx, ly, label, size=11, color=MUTED, anchor=label_anchor, italic=True)

    def render(self):
        head = (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
            f'viewBox="0 0 {self.w} {self.h}" role="img" aria-label="{escape(self.title)}">\n'
            f'<title>{escape(self.title)}</title>\n'
            '<defs><marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" '
            'markerHeight="8" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" '
            f'fill="{INK}"/></marker></defs>\n'
            f'<rect width="{self.w}" height="{self.h}" fill="#ffffff"/>\n')
        return head + "\n".join(self.parts) + "\n</svg>\n"

    def save(self):
        with open(os.path.join(OUT, self.name), "w", encoding="utf8") as f:
            f.write(self.render())

    def check(self):
        problems = []
        for bid, x, y, w, h, lines in self.boxes:
            for k, (s, size, mono) in enumerate(lines):
                tw = text_width(s, size, mono) * (1.1 if k == 0 else 1.0)  # titles are bold
                if tw > w - 14:
                    problems.append(f"{self.name}: text too wide in '{bid}' ({tw:.0f} > {w - 14}): {s}")
            need = sum(sz + 5 for _, sz, _ in lines) + 6
            if need > h:
                problems.append(f"{self.name}: box '{bid}' too short ({need} > {h})")
            if x < 0 or y < 0 or x + w > self.w or y + h > self.h:
                problems.append(f"{self.name}: box '{bid}' outside the canvas")
        def bbox_of_text(x, y, s, size, anchor, mono):
            w = text_width(s, size, mono)
            x0 = x if anchor == "start" else (x - w / 2 if anchor == "middle" else x - w)
            return (x0, y - size * 0.8, w, size)

        def hit(r, px, py, m=1.5):
            x, y, w, h = r
            return x + m < px < x + w - m and y + m < py < y + h - m

        rects = [(b[0], b[1:5]) for b in self.boxes]
        # free text must not run into a box, run off the canvas, or touch another free text
        lb = [(t[2], bbox_of_text(*t)) for t in self.loose]
        for s_, r in lb:
            x, y, w, h = r
            if x < 2 or x + w > self.w - 2:
                problems.append(f"{self.name}: free text off the canvas: {s_}")
            for bid, br in rects:
                bx, by, bw, bh = br
                if x < bx + bw and bx < x + w and y < by + bh and by < y + h:
                    problems.append(f"{self.name}: free text '{s_}' overlaps box '{bid}'")
        for i, (s1, r1) in enumerate(lb):
            for s2, r2 in lb[i + 1:]:
                if r1[0] < r2[0] + r2[2] and r2[0] < r1[0] + r1[2] and r1[1] < r2[1] + r2[3] and r2[1] < r1[1] + r1[3]:
                    problems.append(f"{self.name}: free texts overlap: '{s1}' / '{s2}'")
        # an arrow segment may only touch the boxes at its two ends
        for pts in self.arrows:
            ends = [p for p in (pts[0], pts[-1])]
            touching = {bid for bid, br in rects for (ex, ey) in ends
                        if br[0] - 3 <= ex <= br[0] + br[2] + 3 and br[1] - 3 <= ey <= br[1] + br[3] + 3}
            for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                for k in range(1, 40):
                    px, py = x1 + (x2 - x1) * k / 40, y1 + (y2 - y1) * k / 40
                    for bid, br in rects:
                        if bid not in touching and hit(br, px, py):
                            problems.append(f"{self.name}: arrow {pts[0]}->{pts[-1]} crosses box '{bid}'")
                            break
                    else:
                        continue
                    break
            # and must not run through free text
            for s_, r in lb:
                for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                    if any(hit(r, x1 + (x2 - x1) * k / 40, y1 + (y2 - y1) * k / 40, m=0) for k in range(1, 40)):
                        problems.append(f"{self.name}: arrow {pts[0]}->{pts[-1]} runs through text '{s_}'")
                        break
        for i, a in enumerate(self.boxes):
            for b in self.boxes[i + 1:]:
                ax, ay, aw, ah_ = a[1:5]
                bx, by, bw, bh = b[1:5]
                if ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah_:
                    problems.append(f"{self.name}: boxes '{a[0]}' and '{b[0]}' overlap")
        return problems


def legend(s, x, y, items):
    """items: [(role, label)] drawn as small swatches in one row."""
    cx = x
    for role, label in items:
        s.rect(cx, y - 11, 22, 14, role, rx=3)
        s.text(cx + 30, y, label, size=11, anchor="start", color=MUTED)
        cx += 30 + text_width(label, 11) + 26


# --------------------------------------------------------------------------------------------
def overview():
    s = Svg("architecture-overview.svg", 1000, 560, "plugin-discovery architecture overview")
    s.text(500, 32, "One interface per domain, many backends behind it, loaded per configuration",
           size=16, weight="bold")

    s.group("g_cons", 250, 52, 730, 92, "Consumers")
    s.box("c1", 266, 80, 220, 50, "consumer", "Customer application", ["C++ API directly"])
    s.box("c2", 502, 80, 220, 50, "consumer", "Customer middleware", ["non-ROS"])
    s.box("c3", 738, 80, 226, 50, "consumer", "ROS 2 node", ["e.g. qrb_ros_camera"])

    s.box("api", 250, 170, 730, 56, "iface", "Domain interface",
          ["public, versioned C++ API — the only thing consumers and backends share"])

    s.group("g_be", 250, 252, 730, 104, "")
    s.box("b1", 266, 266, 170, 76, "backend", "QCOM backend", ["vendor SDK A", "e.g. CamX"])
    s.box("b2", 448, 266, 170, 74, "backend", "QCOM backend", ["vendor SDK B", "e.g. QCarCam"])
    s.box("b3", 630, 266, 170, 74, "customer", "Customer backend", ["preferred over QCOM"], stroke_w=2.4)
    s.box("b4", 812, 266, 152, 74, "backend", "… more", ["no consumer change"], dashed=True)

    for i, (x, label) in enumerate(((266, "Vendor SDK A"), (448, "Vendor SDK B"), (630, "3rd-party SDK"))):
        s.box(f"v{i}", x, 398, 170, 40, "vendor", label, title_size=12)
    s.box("hw", 250, 462, 730, 36, "hw", "Hardware / kernel", title_size=13)

    s.box("pd", 20, 170, 206, 202, "core", "plugin_discovery",
          ["ROS-free, shared by", "every domain", "", "knows software stacks,", "not SoCs",
           "", "loads the configured", "backends, collects", "their devices"], size=11)
    s.box("cfg", 20, 398, 206, 66, "config", "Configuration",
          ["platform package ·", "customer · manual"], size=11)

    # consumers -> interface; interface implemented by backends
    s.arrow([(615, 144), (615, 168)])
    for x in (300, 482, 664, 846):
        s.arrow([(x, 264), (x, 228)], dashed=True, color=MUTED)
    s.text(308, 248, "implement", size=11, color=MUTED, italic=True, anchor="start")
    s.text(812, 384, "one per vendor SDK, side by side", size=11, color=MUTED, italic=True, anchor="start")
    for x in (351, 533, 715):
        s.arrow([(x, 342), (x, 396)])
        s.arrow([(x, 438), (x, 460)])
    # plugin_discovery
    s.arrow([(250, 98), (123, 98), (123, 168)], label="ask for backends", label_at=(186, 92))
    s.arrow([(123, 398), (123, 374)], color="#fb8c00", label="decides", label_at=(160, 390))
    s.arrow([(226, 303), (264, 303)], color="#5e35b1", width=2.2)
    s.text(230, 296, "loads", size=11, color="#5e35b1", italic=True, anchor="start")

    legend(s, 20, 540, [("consumer", "consumer"), ("iface", "interface"), ("core", "plugin_discovery"),
                        ("backend", "QCOM backend"), ("customer", "customer backend"),
                        ("config", "configuration"), ("vendor", "vendor / hardware")])
    return s


# --------------------------------------------------------------------------------------------
def runtime():
    s = Svg("runtime-loading.svg", 1000, 520, "plugin-discovery runtime loading flow")
    s.text(500, 32, "Runtime: configuration decides what loads, each backend reports its devices",
           size=16, weight="bold")

    s.group("g_cfg", 20, 56, 250, 300, "1  Configuration (higher wins)")
    s.box("k1", 36, 88, 218, 70, "config", "Manual", ["load({...}) or", "PLUGIN_DISCOVERY_BACKEND_<D>"],
          size=11)
    s.box("k2", 36, 172, 218, 70, "customer", "Customer config", ["/etc/plugin_discovery/", "config.d/*.yaml"],
          size=11, stroke_w=2.4)
    s.box("k3", 36, 256, 218, 84, "config", "Platform config package",
          ["share/plugin_discovery/", "config.d/*.yaml", "(installed by the user)"], size=11)

    s.box("plan", 316, 150, 180, 104, "core", "2  Plan", ["read manifests,", "pick the listed", "backends — no .so", "opened yet"],
          size=11)

    s.group("g_load", 540, 56, 440, 300, "3  Load + probe() each planned backend")
    rows = [("p1", "Backend A", "backend", "ok", "kept: cam0, cam1", 1.6),
            ("p2", "Backend B", "backend", "skip", "unsupported: SDK not on this SoC", 1.6),
            ("p3", "Customer backend", "customer", "ok", "kept: usb0", 2.4),
            ("p4", "Backend C", "backend", "skip", "fault: device not answering", 1.6)]
    for k, (bid, title, role, res, why, sw) in enumerate(rows):
        y = 84 + k * 66
        s.box(bid, 556, y, 172, 52, role, title, ["probe()"], size=11, stroke_w=sw)
        s.box(bid + "r", 760, y, 204, 52, res, "Kept" if res == "ok" else "Skipped", [why], size=11)
        s.arrow([(728, y + 26), (758, y + 26)], color="#43a047" if res == "ok" else "#e53935")

    s.box("out", 316, 392, 664, 64, "consumer", "4  BackendSet → consumer",
          ["the kept backends with their devices; skipped ones are listed with their reason"],
          size=12)

    s.arrow([(270, 202), (314, 202)])
    s.arrow([(496, 202), (538, 202)], color="#5e35b1", width=2.2, label="dlopen", label_at=(517, 194))
    s.arrow([(760, 356), (760, 390)])

    s.box("n1", 20, 392, 250, 64, "note", "No SoC check here",
          ["each backend decides for itself", "whether it runs on this machine"], size=11)
    legend(s, 20, 500, [("config", "configuration"), ("core", "plugin_discovery"), ("backend", "backend"),
                        ("customer", "customer"), ("ok", "loaded"), ("skip", "skipped")])
    return s


# --------------------------------------------------------------------------------------------
def build_deploy():
    s = Svg("build-and-deploy.svg", 1000, 540, "plugin-discovery build and deployment")
    s.text(500, 32, "Build once, install per target: the packages decide what is on the machine",
           size=16, weight="bold")

    s.group("g_src", 20, 56, 300, 300, "Source")
    s.box("s1", 36, 88, 268, 66, "iface", "Domain interface", ["PLUGIN_DISCOVERY_INTERFACE", "(domain, version)"],
          size=11)
    s.box("s2", 36, 168, 268, 66, "backend", "Backend .cpp", ["PLUGIN_DISCOVERY_REGISTER_BACKEND", "+ probe()"],
          size=11)
    s.box("s3", 36, 248, 268, 92, "config", "CMakeLists.txt",
          ["plugin_discovery_add_plugin(", "TARGET DOMAIN NAME CLASS BASE", "BACKEND ORIGIN …)"], size=11,
          mono_lines=True)

    s.group("g_out", 360, 56, 280, 300, "Build output (per backend)")
    s.box("o1", 376, 90, 248, 66, "backend", "lib<backend>.so", ["records interface version", "+ ABI tag"],
          size=11)
    s.box("o2", 376, 174, 248, 66, "config", "<backend>.plugin.yaml", ["generated, never hand-written"],
          size=11)
    s.box("o3", 376, 266, 248, 74, "config", "Platform config package", ["one per platform"], size=11)

    s.group("g_dev", 680, 56, 300, 300, "On the device")
    s.box("d1", 696, 88, 268, 70, "vendor", "lib/<domain>/", ["backend .so + private vendor libs"], size=11)
    s.box("d2", 696, 172, 268, 70, "vendor", "share/<domain>/plugins/", ["manifests"], size=11)
    s.box("d3", 696, 256, 268, 84, "vendor", "share/plugin_discovery/", ["config.d/   platform package", "/etc/…/config.d/   customer"],
          size=11)

    s.arrow([(304, 121), (374, 121)])
    s.arrow([(304, 201), (340, 201), (340, 135), (374, 135)])
    s.text(339, 112, "compile", size=11, color=MUTED, italic=True)
    s.arrow([(304, 294), (352, 294), (352, 207), (374, 207)])
    s.text(310, 286, "cmake", size=11, color=MUTED, italic=True, anchor="start")
    s.arrow([(624, 123), (694, 123)])
    s.text(659, 115, "install", size=11, color=MUTED, italic=True)
    s.arrow([(624, 207), (694, 207)])
    s.arrow([(624, 303), (694, 303)])

    s.box("ub", 20, 384, 310, 92, "iface", "Ubuntu", ["one image + apt repo for IQ8/9/10", "image has no platform package:",
                                                     "the user installs the right one"], size=11)
    s.box("qli", 345, 384, 310, 92, "iface", "QLI", ["one image per machine", "ships the matching", "configuration"], size=11)
    s.box("cu", 670, 384, 310, 92, "customer", "Customer", ["installs its backend", "+ one config file",
                                                            "→ loaded before QCOM's"], size=11, stroke_w=2.4)
    legend(s, 20, 516, [("iface", "interface / target"), ("backend", "backend"), ("config", "manifest / config"),
                        ("vendor", "installed files"), ("customer", "customer")])
    return s


# --------------------------------------------------------------------------------------------
def coexistence():
    s = Svg("multi-backend-camera.svg", 1000, 500, "Several camera backends in one process")
    s.text(500, 32, "Example: several camera backends in one process, each serving its own devices",
           size=16, weight="bold")

    s.box("node", 20, 56, 730, 56, "consumer", "qrb_ros_camera / CameraManager",
          ["one node for every SoC, the same ROS API on top"], size=12)
    s.box("iface", 20, 136, 960, 50, "iface", "ICameraBackend", ["probe() reports cameras · open(device)"],
          size=12)

    s.group("g_proc", 20, 208, 960, 156, "")
    s.text(964, 380, "one process", size=11, color=MUTED, italic=True, anchor="end")
    cols = [
        ("cam1", "CamX backend", ["QCOM · iq8 / iq9"], "backend", ["/dev/video0", "/dev/video1"], "MIPI cameras"),
        ("cam2", "QCarCam backend", ["QCOM · iq10"], "backend", ["input 0 … 15"], "GMSL cameras"),
        ("cam3", "RealSense backend", ["3rd party"], "backend", ["usb 2-1"], "USB depth camera"),
        ("cam4", "Customer backend", ["customer-owned"], "customer", ["usb 3-2"], "customer camera"),
    ]
    for i, (bid, title, sub, role, devs, hw) in enumerate(cols):
        x = 36 + i * 236
        s.box(bid, x, 222, 220, 64, role, title, sub, size=11, stroke_w=2.4 if role == "customer" else 1.6)
        s.box(bid + "d", x, 298, 220, 52, "ok", "devices", [" · ".join(devs)], size=11, title_size=12)
        s.box(bid + "h", x, 404, 220, 40, "hw", hw, title_size=12)
        s.arrow([(x + 110, 286), (x + 110, 296)], head=False, color=MUTED)
        s.arrow([(x + 110, 350), (x + 110, 402)], color=MUTED)
        s.arrow([(x + 110, 220), (x + 110, 188)], dashed=True, color=MUTED)
    s.arrow([(385, 112), (385, 134)])

    s.box("n", 770, 56, 210, 64, "note", "Same device twice?",
          ["the backend ordered first keeps it"], size=10)
    legend(s, 20, 480, [("consumer", "consumer"), ("iface", "interface"), ("backend", "backend"),
                        ("customer", "customer backend"), ("ok", "reported devices"), ("hw", "hardware")])
    return s


def main():
    diagrams = [overview(), runtime(), build_deploy(), coexistence()]
    problems = []
    for d in diagrams:
        d.save()
        problems += d.check()
        print("wrote", d.name)
    if "--check" in sys.argv:
        for p in problems:
            print("CHECK", p)
        print("check:", "ok" if not problems else f"{len(problems)} problem(s)")
        return 1 if problems else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
