# -*- coding: utf-8 -*-
"""umlkit —— 本任务用来生成 UML 图的小工具（Zhu Ziliang）。

一次定义图的内容，同时输出两种文件：
  1. *.drawio：可编辑源文件，用 draw.io / diagrams.net 打开即可继续修改；
  2. *.png  ：按同一套坐标渲染的高清位图，供报告或 PDF 直接插图。

只实现本任务需要的图元：泳道时序图（参与者、生命线、消息、返回、
自调用、激活条、组合片段 alt/opt、注释）和对象图（对象框、链接、注释）。
"""

import os
import re
from xml.sax.saxutils import escape, quoteattr

from PIL import Image, ImageDraw, ImageFont

FONT_FILES = [
    ("C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/msyhbd.ttc"),
    ("C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/arialbd.ttf"),
]

# 颜色（draw.io 常用配色，导出到 Word 后仍然清晰）
INK = "#1F2933"       # 主线条/文字
MUTED = "#52606D"     # 次要文字
BOX_FILL = "#EAF2FB"  # 参与者/对象框填充
BOX_LINE = "#4A7EBB"  # 参与者/对象框边框
ALT_FILL = "#FFF8E1"  # 可选/备选片段填充
NOTE_FILL = "#FFFDE7"  # 注释填充
NOTE_LINE = "#C9A227"

_font_cache = {}
_measure = ImageDraw.Draw(Image.new("RGB", (8, 8), "white"))


def font(size_px, bold=False):
    key = (int(round(size_px)), bold)
    if key not in _font_cache:
        for regular, bold_file in FONT_FILES:
            path = bold_file if bold else regular
            if os.path.exists(path):
                try:
                    _font_cache[key] = ImageFont.truetype(path, key[0])
                    break
                except OSError:
                    continue
        else:
            _font_cache[key] = ImageFont.load_default()
    return _font_cache[key]


def text_width(text, size, bold=False):
    return _measure.textlength(text, font=font(size, bold))


def wrap_text(text, size, max_width, bold=False):
    """按像素宽度换行。中文逐字切，英文按空格切。"""
    lines = []
    for paragraph in text.split("\n"):
        if not paragraph:
            lines.append("")
            continue
        if text_width(paragraph, size, bold) <= max_width:
            lines.append(paragraph)
            continue
        current = ""
        for token in re.findall(r"[A-Za-z0-9_\-\.\(\)\[\]'\"+*/=<>:,]+|\s+|.", paragraph):
            if text_width(current + token, size, bold) <= max_width:
                current += token
                continue
            if current.strip():
                lines.append(current.rstrip())
                current = token.lstrip()
            else:
                lines.append(current)
                current = token
        if current:
            lines.append(current.rstrip())
    return lines


class Canvas:
    """收集图元，最后分别渲染为 PNG 和 draw.io 文件。"""

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.items = []

    # ---- 图元 ----
    def rect(self, x, y, w, h, fill=BOX_FILL, stroke=BOX_LINE, width=1.2,
             dashed=False, rounded=False):
        self.items.append(dict(kind="rect", x=x, y=y, w=w, h=h, fill=fill,
                               stroke=stroke, width=width, dashed=dashed,
                               rounded=rounded, text="", font_size=10,
                               bold=False, color=INK, align="center"))

    def text(self, x, y, value, size=10, bold=False, color=INK,
             align="center", width=None):
        self.items.append(dict(kind="text", x=x, y=y, w=width or 0, h=0,
                               value=value, font_size=size, bold=bold,
                               color=color, align=align, text=value))

    def line(self, x1, y1, x2, y2, color=INK, width=1.2, dashed=False,
             label="", label_size=9, label_align="center"):
        self.items.append(dict(kind="line", x1=x1, y1=y1, x2=x2, y2=y2,
                               color=color, width=width, dashed=dashed,
                               label=label, font_size=label_size,
                               align=label_align))

    def arrow(self, x1, y1, x2, y2, kind="call", color=INK, width=1.2,
              label="", label_size=9, label_shift=6):
        self.items.append(dict(kind="arrow", x1=x1, y1=y1, x2=x2, y2=y2,
                               arrow=kind, color=color, width=width,
                               label=label, font_size=label_size,
                               label_shift=label_shift))

    def polyline(self, points, color=INK, width=1.2, arrow=None):
        self.items.append(dict(kind="polyline", points=points, color=color,
                               width=width, arrow=arrow))

    def actor(self, x, y, w, h, label, size=10, bold=False):
        self.items.append(dict(kind="actor", x=x, y=y, w=w, h=h, text=label,
                               font_size=size, bold=bold, color=INK))

    def note(self, x, y, w, h, value, size=9):
        self.items.append(dict(kind="note", x=x, y=y, w=w, h=h, text=value,
                               font_size=size))

    def frame(self, x, y, w, h, kind, label="", size=9):
        self.items.append(dict(kind="frame", x=x, y=y, w=w, h=h, text=label,
                               frame_kind=kind, font_size=size))

    def frame_divider(self, x1, y, x2, label="", size=9):
        self.items.append(dict(kind="divider", x1=x1, y1=y, x2=x2, y2=y,
                               text=label, font_size=size))

    # ---- PNG ----
    def render_png(self, path, scale=3.0, margin=24):
        w = int((self.width + margin * 2) * scale)
        h = int((self.height + margin * 2) * scale)
        image = Image.new("RGB", (w, h), "white")
        draw = ImageDraw.Draw(image)

        def px(value):
            return (value + margin) * scale

        for item in self.items:
            kind = item["kind"]
            if kind == "rect":
                x, y = px(item["x"]), px(item["y"])
                x2, y2 = px(item["x"] + item["w"]), px(item["y"] + item["h"])
                box = [x, y, x2, y2]
                radius = min(x2 - x, y2 - y) * 0.12 if item["rounded"] else 0
                if item["dashed"] and item["fill"] in ("", None):
                    self._dashed_rect(draw, box, item, scale)
                else:
                    if radius > 1:
                        draw.rounded_rectangle(box, radius=radius,
                                               fill=item["fill"],
                                               outline=item["stroke"],
                                               width=max(1, int(item["width"] * scale)))
                    else:
                        draw.rectangle(box, fill=item["fill"],
                                       outline=item["stroke"],
                                       width=max(1, int(item["width"] * scale)))
                if item["text"]:
                    fnt = font(item["font_size"] * scale, item["bold"])
                    lines = wrap_text(item["text"], item["font_size"] * scale,
                                      (item["w"] - 12) * scale, item["bold"])
                    line_h = fnt.size * 1.45
                    start_y = (item["y"] + item["h"] / 2) * scale + margin * scale \
                        - line_h * len(lines) / 2 + line_h / 2
                    for index, line in enumerate(lines):
                        draw.text((px(item["x"] + item["w"] / 2),
                                   start_y + index * line_h),
                                  line, font=fnt, fill=item["color"], anchor="mm")
            elif kind == "text":
                fnt = font(item["font_size"] * scale, item["bold"])
                lines = item["value"].split("\n")
                if item["w"]:
                    lines = wrap_text(item["value"], item["font_size"] * scale,
                                      item["width"] * scale, item["bold"])
                line_h = fnt.size * 1.45
                total = line_h * len(lines)
                start_y = px(item["y"]) - total / 2 + line_h / 2
                for index, line in enumerate(lines):
                    if item["align"] == "left":
                        draw.text((px(item["x"]), start_y + index * line_h),
                                  line, font=fnt, fill=item["color"], anchor="lm")
                    elif item["align"] == "right":
                        draw.text((px(item["x"]), start_y + index * line_h),
                                  line, font=fnt, fill=item["color"], anchor="rm")
                    else:
                        draw.text((px(item["x"]), start_y + index * line_h),
                                  line, font=fnt, fill=item["color"], anchor="mm")
            elif kind == "line":
                if item["dashed"]:
                    self._dashed_line(draw, px(item["x1"]), px(item["y1"]),
                                      px(item["x2"]), px(item["y2"]),
                                      item["color"], item["width"] * scale)
                else:
                    draw.line([px(item["x1"]), px(item["y1"]),
                               px(item["x2"]), px(item["y2"])],
                              fill=item["color"],
                              width=max(1, int(item["width"] * scale)))
                if item.get("label"):
                    mx = (item["x1"] + item["x2"]) / 2
                    my = (item["y1"] + item["y2"]) / 2
                    draw.text((px(mx), px(my)), item["label"],
                              font=font(item["font_size"] * scale),
                              fill=MUTED, anchor="mm")
            elif kind == "arrow":
                self._arrow(draw, px(item["x1"]), px(item["y1"]),
                            px(item["x2"]), px(item["y2"]), item, scale)
                if item.get("label"):
                    mid_x = (item["x1"] + item["x2"]) / 2
                    label_y = item["y1"] - item["label_shift"]
                    # 白底压住生命线，保证标签可读
                    text_w = text_width(item["label"], item["font_size"] * scale)
                    draw.rectangle([px(mid_x) - text_w / 2 - 4, px(label_y) - 8 * scale,
                                    px(mid_x) + text_w / 2 + 4, px(label_y) + 8 * scale],
                                   fill="white")
                    draw.text((px(mid_x), px(label_y)), item["label"],
                              font=font(item["font_size"] * scale),
                              fill=INK, anchor="mm")
            elif kind == "polyline":
                points = [(px(x), px(y)) for x, y in item["points"]]
                draw.line(points, fill=item["color"],
                          width=max(1, int(item["width"] * scale)))
                if item["arrow"] == "open":
                    self._open_head(draw, points[-2], points[-1],
                                    item["color"], scale)
            elif kind == "actor":
                self._actor(draw, px(item["x"]), px(item["y"]),
                            item["w"] * scale, item["h"] * scale, item, scale)
            elif kind == "note":
                x, y = px(item["x"]), px(item["y"])
                draw.rectangle([x, y, px(item["x"] + item["w"]),
                                px(item["y"] + item["h"])],
                               fill=NOTE_FILL, outline=NOTE_LINE,
                               width=max(1, int(scale)))
                fnt = font(item["font_size"] * scale)
                lines = wrap_text(item["text"], item["font_size"] * scale,
                                  (item["w"] - 12) * scale)
                line_h = fnt.size * 1.4
                start_y = y + (item["h"] * scale - line_h * len(lines)) / 2 + line_h / 2
                for index, line in enumerate(lines):
                    draw.text((x + 6 * scale, start_y + index * line_h), line,
                              font=fnt, fill=INK, anchor="lm")
            elif kind == "frame":
                x, y = px(item["x"]), px(item["y"])
                w, h = item["w"] * scale, item["h"] * scale
                draw.rectangle([x, y, x + w, y + h], outline=INK,
                               width=max(1, int(1.3 * scale)))
                fnt = font(item["font_size"] * scale, True)
                kind_w = text_width(item["frame_kind"], item["font_size"] * scale, True) + 16 * scale
                tab_h = fnt.size * 1.7
                draw.polygon([(x, y), (x + kind_w, y), (x + kind_w, y + tab_h),
                              (x + kind_w - 10 * scale, y + tab_h),
                              (x, y + tab_h)],
                             fill="white", outline=INK,
                             width=max(1, int(1.2 * scale)))
                draw.text((x + 8 * scale, y + tab_h / 2), item["frame_kind"],
                          font=fnt, fill=INK, anchor="lm")
                if item["text"]:
                    draw.text((x + kind_w + 10 * scale, y + tab_h / 2),
                              item["text"], font=font(item["font_size"] * scale),
                              fill=INK, anchor="lm")
            elif kind == "divider":
                self._dashed_line(draw, px(item["x1"]), px(item["y1"]),
                                  px(item["x2"]), px(item["y2"]), INK,
                                  scale)
                if item["text"]:
                    draw.text((px(item["x1"]) + 8 * scale, px(item["y1"]) - 9 * scale),
                              item["text"], font=font(item["font_size"] * scale),
                              fill=INK, anchor="lm")

        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        image.save(path)
        return image.size

    @staticmethod
    def _dashed_line(draw, x1, y1, x2, y2, color, width, dash=7, gap=5):
        import math
        length = math.hypot(x2 - x1, y2 - y1)
        if length == 0:
            return
        ux, uy = (x2 - x1) / length, (y2 - y1) / length
        position = 0.0
        while position < length:
            end = min(position + dash, length)
            draw.line([x1 + ux * position, y1 + uy * position,
                       x1 + ux * end, y1 + uy * end],
                      fill=color, width=max(1, int(width)))
            position += dash + gap

    def _dashed_rect(self, draw, box, item, scale):
        x1, y1, x2, y2 = box
        for (a, b, c, d) in [(x1, y1, x2, y1), (x2, y1, x2, y2),
                             (x2, y2, x1, y2), (x1, y2, x1, y1)]:
            self._dashed_line(draw, a, b, c, d, item["stroke"],
                              item["width"] * scale)

    def _arrow_head(self, draw, x1, y1, x2, y2, color, scale, size=7.5):
        import math
        angle = math.atan2(y2 - y1, x2 - x1)
        length = size * scale
        spread = math.radians(24)
        points = [(x2, y2),
                  (x2 - length * math.cos(angle - spread),
                   y2 - length * math.sin(angle - spread)),
                  (x2 - length * math.cos(angle + spread),
                   y2 - length * math.sin(angle + spread))]
        draw.polygon(points, fill=color, outline=color)

    def _open_head(self, draw, start, end, color, scale, size=8.0):
        import math
        x1, y1 = start
        x2, y2 = end
        angle = math.atan2(y2 - y1, x2 - x1)
        length = size * scale
        spread = math.radians(22)
        for sign in (-1, 1):
            draw.line([x2, y2,
                       x2 - length * math.cos(angle + sign * spread),
                       y2 - length * math.sin(angle + sign * spread)],
                      fill=color, width=max(1, int(1.2 * scale)))

    def _arrow(self, draw, x1, y1, x2, y2, item, scale):
        if item.get("dashed"):
            self._dashed_line(draw, x1, y1, x2, y2, item["color"],
                              item["width"] * scale)
        else:
            draw.line([x1, y1, x2, y2], fill=item["color"],
                      width=max(1, int(item["width"] * scale)))
        if item["arrow"] == "call":
            self._arrow_head(draw, x1, y1, x2, y2, item["color"], scale)
        elif item["arrow"] == "return":
            self._open_head(draw, (x1, y1), (x2, y2), item["color"], scale)
        elif item["arrow"] == "async":
            self._open_head(draw, (x1, y1), (x2, y2), item["color"], scale)

    def _actor(self, draw, x, y, w, h, item, scale):
        cx = x + w / 2
        head_r = min(w, h) * 0.16
        head_cy = y + head_r + 2 * scale
        draw.ellipse([cx - head_r, head_cy - head_r, cx + head_r, head_cy + head_r],
                     outline=item["color"], width=max(1, int(1.3 * scale)))
        body_top = head_cy + head_r
        body_bottom = y + h * 0.68
        draw.line([cx, body_top, cx, body_bottom], fill=item["color"],
                  width=max(1, int(1.3 * scale)))
        arm_y = body_top + (body_bottom - body_top) * 0.32
        draw.line([cx - w * 0.30, arm_y, cx + w * 0.30, arm_y],
                  fill=item["color"], width=max(1, int(1.3 * scale)))
        draw.line([cx, body_bottom, cx - w * 0.28, y + h],
                  fill=item["color"], width=max(1, int(1.3 * scale)))
        draw.line([cx, body_bottom, cx + w * 0.28, y + h],
                  fill=item["color"], width=max(1, int(1.3 * scale)))
        fnt = font(item["font_size"] * scale, item["bold"])
        draw.text((cx, y + h + fnt.size * 0.95), item["text"], font=fnt,
                  fill=INK, anchor="mm")

    # ---- draw.io ----
    def render_drawio(self, path, name="diagram", margin=24):
        cells = []
        for index, item in enumerate(self.items):
            cells.append(self._cell(item, "c%d" % index, margin))

        body = "\n".join(cells)
        page_w = int(self.width + margin * 2)
        page_h = int(self.height + margin * 2)
        content = (
            '<mxfile host="app.diagrams.net" type="device">\n'
            '  <diagram id="%s" name=%s>\n'
            '    <mxGraphModel dx="1600" dy="1000" grid="1" gridSize="10" '
            'guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" '
            'pageScale="1" pageWidth="%d" pageHeight="%d" math="0" shadow="0">\n'
            '      <root>\n'
            '        <mxCell id="0"/>\n'
            '        <mxCell id="1" parent="0"/>\n'
            '%s\n'
            '      </root>\n'
            '    </mxGraphModel>\n'
            '  </diagram>\n'
            '</mxfile>\n'
        ) % (name, quoteattr(name), page_w, page_h, body)
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)

    def _geom(self, x, y, w, h, margin):
        return ('<mxGeometry x="%g" y="%g" width="%g" height="%g" as="geometry"/>'
                % (x + margin, y + margin, max(w, 1), max(h, 1)))

    def _cell(self, item, cell_id, margin):
        kind = item["kind"]
        if kind == "rect":
            style = ("rounded=%d;whiteSpace=wrap;html=0;fillColor=%s;"
                     "strokeColor=%s;strokeWidth=%g;fontSize=%g;fontColor=%s;"
                     "fontFamily=Microsoft YaHei;align=center;verticalAlign=middle;"
                     "dashed=%d;"
                     % (1 if item["rounded"] else 0, item["fill"], item["stroke"],
                        item["width"], item["font_size"], item["color"],
                        1 if item["dashed"] else 0))
            return ('        <mxCell id="%s" value=%s style=%s vertex="1" parent="1">\n'
                    '          %s\n'
                    '        </mxCell>' % (cell_id, quoteattr(item["text"]), quoteattr(style),
                                           self._geom(item["x"], item["y"], item["w"],
                                                      item["h"], margin)))
        if kind == "text":
            style = ("text;html=0;whiteSpace=wrap;fontSize=%g;fontStyle=%d;"
                     "fontColor=%s;align=%s;verticalAlign=middle;"
                     "fontFamily=Microsoft YaHei;"
                     % (item["font_size"], 1 if item["bold"] else 0,
                        item["color"], item["align"]))
            width = item["w"] or max(text_width(item["value"], item["font_size"], item["bold"]) + 30, 80)
            height = item["font_size"] * 1.5 * max(1, len(item["value"].split("\n")))
            x = item["x"] - (width / 2 if item["align"] == "center" else 0)
            return ('        <mxCell id="%s" value=%s style=%s vertex="1" parent="1">\n'
                    '          %s\n'
                    '        </mxCell>' % (cell_id, quoteattr(item["value"]), quoteattr(style),
                                           self._geom(x, item["y"] - height / 2, width,
                                                      height, margin)))
        if kind == "actor":
            style = ("shape=umlActor;verticalLabelPosition=bottom;verticalAlign=top;"
                     "html=0;fillColor=none;strokeColor=%s;strokeWidth=1.3;"
                     "fontSize=%g;fontStyle=%d;fontFamily=Microsoft YaHei;"
                     % (INK, item["font_size"], 1 if item["bold"] else 0))
            return ('        <mxCell id="%s" value=%s style=%s vertex="1" parent="1">\n'
                    '          %s\n'
                    '        </mxCell>' % (cell_id, quoteattr(item["text"]), quoteattr(style),
                                           self._geom(item["x"], item["y"], item["w"],
                                                      item["h"], margin)))
        if kind == "note":
            style = ("shape=note;whiteSpace=wrap;html=0;size=12;fillColor=%s;"
                     "strokeColor=%s;align=left;verticalAlign=middle;fontSize=%g;"
                     "fontFamily=Microsoft YaHei;spacingLeft=6;"
                     % (NOTE_FILL, NOTE_LINE, item["font_size"]))
            return ('        <mxCell id="%s" value=%s style=%s vertex="1" parent="1">\n'
                    '          %s\n'
                    '        </mxCell>' % (cell_id, quoteattr(item["text"]), quoteattr(style),
                                           self._geom(item["x"], item["y"], item["w"],
                                                      item["h"], margin)))
        if kind == "frame":
            style = ("shape=umlFrame;whiteSpace=wrap;html=0;width=110;height=26;"
                     "fillColor=none;strokeColor=%s;fontSize=%g;fontStyle=1;"
                     "fontFamily=Microsoft YaHei;align=center;verticalAlign=middle;"
                     % (INK, item["font_size"]))
            value = item["frame_kind"] + (("\n" + item["text"]) if item["text"] else "")
            return ('        <mxCell id="%s" value=%s style=%s vertex="1" parent="1">\n'
                    '          %s\n'
                    '        </mxCell>' % (cell_id, quoteattr(value), quoteattr(style),
                                           self._geom(item["x"], item["y"], item["w"],
                                                      item["h"], margin)))
        if kind in ("line", "arrow", "divider"):
            if kind == "divider":
                style = ("endArrow=none;html=0;dashed=1;strokeColor=%s;"
                         "labelBackgroundColor=#FFFFFF;fontSize=%g;"
                         "fontFamily=Microsoft YaHei;"
                         % (INK, item["font_size"]))
                value = item["text"]
            else:
                dashed = 1 if (kind == "arrow" and item.get("arrow") == "return") else \
                    (1 if item.get("dashed") else 0)
                if kind == "arrow" and item["arrow"] == "call":
                    ends = "endArrow=block;endFill=1;"
                elif kind == "arrow":
                    ends = "endArrow=open;endFill=0;"
                else:
                    ends = "endArrow=none;"
                style = ("%shtml=0;dashed=%d;strokeColor=%s;strokeWidth=%g;"
                         "labelBackgroundColor=#FFFFFF;fontSize=%g;"
                         "fontFamily=Microsoft YaHei;"
                         % (ends, dashed, item["color"], item["width"],
                            item["font_size"]))
                value = item.get("label", "")
            return ('        <mxCell id="%s" value=%s style=%s edge="1" parent="1">\n'
                    '          <mxGeometry relative="1" as="geometry">\n'
                    '            <mxPoint x="%g" y="%g" as="sourcePoint"/>\n'
                    '            <mxPoint x="%g" y="%g" as="targetPoint"/>\n'
                    '          </mxGeometry>\n'
                    '        </mxCell>'
                    % (cell_id, quoteattr(value), quoteattr(style),
                       item["x1"] + margin, item["y1"] + margin,
                       item["x2"] + margin, item["y2"] + margin))
        if kind == "polyline":
            # 自调用消息：用 waypoints 保留折线形状
            points = item["points"]
            way = "".join(
                '            <mxPoint x="%g" y="%g"/>\n'
                % (x + margin, y + margin) for x, y in points[1:-1])
            style = ("endArrow=block;endFill=1;html=0;strokeColor=%s;"
                     "strokeWidth=%g;labelBackgroundColor=#FFFFFF;"
                     "fontFamily=Microsoft YaHei;"
                     % (item["color"], item["width"]))
            return ('        <mxCell id="%s" value="" style=%s edge="1" parent="1">\n'
                    '          <mxGeometry relative="1" as="geometry">\n'
                    '            <mxPoint x="%g" y="%g" as="sourcePoint"/>\n'
                    '            <mxPoint x="%g" y="%g" as="targetPoint"/>\n'
                    '            <Array as="points">\n%s            </Array>\n'
                    '          </mxGeometry>\n'
                    '        </mxCell>'
                    % (cell_id, quoteattr(style),
                       points[0][0] + margin, points[0][1] + margin,
                       points[-1][0] + margin, points[-1][1] + margin, way))
        return ""

    def save(self, png_path, drawio_path, name, scale=3.0):
        size = self.render_png(png_path, scale=scale)
        self.render_drawio(drawio_path, name=name)
        return size


class SequenceDiagram:
    """时序图：参与者气泡、生命线、消息、返回、自调用、组合片段。"""

    GAP = 46          # 消息之间的垂直间距
    HEADER_H = 34     # 参与者框高度
    TOP = 58          # 标题下方到参与者框的距离
    PAD = 26          # 参与者框左右内边距

    def __init__(self, title, subtitle=""):
        self.title = title
        self.subtitle = subtitle
        self.parts = []      # {name, kind, width}
        self.messages = []   # {src, dst, label, kind, self_ref}
        self.fragments = []
        self.notes = []      # {part, text, after_msg, side}
        self.activations = []  # {part, first, last}

    # ---- 定义 ----
    def participant(self, name, kind="object"):
        width = max(96, text_width(name, 10.5) + self.PAD + 8)
        self.parts.append(dict(name=name, kind=kind, width=width))
        return len(self.parts) - 1

    def message(self, src, dst, label, kind="call"):
        self.messages.append(dict(src=src, dst=dst, label=label, kind=kind,
                                  self_ref=(src == dst)))
        return len(self.messages) - 1

    def ret(self, src, dst, label):
        return self.message(src, dst, label, kind="return")

    def self_message(self, part, label):
        self.messages.append(dict(src=part, dst=part, label=label, kind="call",
                                  self_ref=True))
        return len(self.messages) - 1

    def activation(self, part, first_msg, last_msg):
        self.activations.append(dict(part=part, first=first_msg, last=last_msg))

    def fragment(self, kind, label, first_msg, last_msg, dividers=None):
        self.fragments.append(dict(kind=kind, label=label, first=first_msg,
                                   last=last_msg, dividers=dividers or []))

    def note(self, part, text, after_msg, width=190, side="right"):
        self.notes.append(dict(part=part, text=text, after=after_msg,
                               width=width, side=side))

    # ---- 布局 ----
    def build(self):
        # 参与者水平排布
        xs = []
        cursor = self.PAD
        for part in self.parts:
            xs.append(cursor + part["width"] / 2)
            cursor += part["width"] + 34
        width = cursor - 34 + self.PAD
        header_y = self.TOP

        # 画布右侧要容纳自调用的标签与靠右的注释
        extra_right = 0
        for message in self.messages:
            if message["self_ref"]:
                extra_right = max(
                    extra_right,
                    xs[message["src"]] + 52 + 8 +
                    text_width(message["label"], 9.2) + 14)
        for note in self.notes:
            if note.get("side", "right") == "right":
                extra_right = max(extra_right,
                                  xs[note["part"]] + 14 + note["width"] + 14)
        width = max(width, extra_right)

        # 逐个消息分配 y（附加注释的地方额外留出空间，避免压住后面的箭头）
        note_space = {}
        for note in self.notes:
            lines = wrap_text(note["text"], 9, note["width"] - 16)
            note["_lines"] = lines
            note["_h"] = max(32, 12 + len(lines) * 13)
            note_space[note["after"]] = note_space.get(note["after"], 0) + \
                note["_h"] + 16

        y = header_y + self.HEADER_H + 56
        positions = []
        for index, message in enumerate(self.messages):
            positions.append(y)
            y += self.GAP if not message["self_ref"] else self.GAP + 12
            y += note_space.get(index, 0)
        fragment_bottom = max(
            [positions[fragment["last"]] + 22 for fragment in self.fragments] or [0])
        last_message_bottom = positions[-1] + 34 if positions else 0
        body_bottom = max(last_message_bottom, fragment_bottom + 26)
        height = body_bottom

        canvas = Canvas(width, height)
        canvas.text(width / 2, 16, self.title, size=15, bold=True)
        if self.subtitle:
            canvas.text(width / 2, 36, self.subtitle, size=9.5, color=MUTED)

        # 参与者框 / 小人
        for index, part in enumerate(self.parts):
            cx = xs[index]
            if part["kind"] == "actor":
                actor_h = 44
                canvas.actor(cx - 14, header_y - 6, 28, actor_h, part["name"],
                             size=10)
                life_top = header_y - 6 + actor_h + 18
            else:
                box_h = self.HEADER_H
                x = cx - part["width"] / 2
                item = dict(kind="rect", x=x, y=header_y - box_h / 2,
                            w=part["width"], h=box_h, fill=BOX_FILL,
                            stroke=BOX_LINE, width=1.3, dashed=False,
                            rounded=True, text=part["name"], font_size=10.5,
                            bold=(part["kind"] == "classifier"), color=INK,
                            align="center")
                canvas.items.append(item)
                life_top = header_y + box_h / 2
            canvas.line(cx, life_top, cx, body_bottom, color="#9AA5B1",
                        width=1.0, dashed=True)
            # 生命线底部再画一个小方块（对象销毁不是本图主题，仅作收尾）
            canvas.rect(cx - 4, body_bottom - 4, 8, 8, fill="white",
                        stroke="#9AA5B1", width=0.9)

        # 先给消息写入 y，便于激活条与注释定位
        for message, position in zip(self.messages, positions):
            message["_y"] = position
        for activation in self.activations:
            top = self.messages[activation["first"]]["_y"] - 8
            bottom = self.messages[activation["last"]]["_y"] + 8
            cx = xs[activation["part"]]
            canvas.rect(cx - 5, top, 10, max(bottom - top, 14), fill="#FFFFFF",
                        stroke=INK, width=1.0)

        # 消息箭头
        for message in self.messages:
            mx = message["_y"]
            src_x, dst_x = xs[message["src"]], xs[message["dst"]]
            if message["self_ref"]:
                loop = 52
                canvas.polyline([(src_x + 5, mx - 8), (src_x + loop, mx - 8),
                                 (src_x + loop, mx + 12), (src_x + 5, mx + 12)],
                                color=INK, width=1.2, arrow="open")
                canvas.text(src_x + loop + 8, mx + 2, message["label"],
                            size=9, align="left")
                continue
            arrow_kind = {"return": "return", "async": "async"}.get(
                message["kind"], "call")
            canvas.arrow(src_x, mx, dst_x, mx, kind=arrow_kind,
                         label=message["label"], label_size=9.2,
                         label_shift=9)
            # 返回箭头用虚线
            if message["kind"] == "return":
                canvas.items[-1]["dashed"] = True

        # 组合片段
        for fragment in self.fragments:
            involved = set()
            for index in range(fragment["first"], fragment["last"] + 1):
                message = self.messages[index]
                involved.add(message["src"])
                involved.add(message["dst"])
            left = min(xs[index] for index in involved) - 26
            right = max(xs[index] for index in involved) + 26
            top = self.messages[fragment["first"]]["_y"] - 38
            bottom = self.messages[fragment["last"]]["_y"] + 22
            canvas.frame(left, top, right - left, bottom - top,
                         fragment["kind"], fragment["label"], size=9)
            for divider_msg, divider_label in fragment["dividers"]:
                y_divider = self.messages[divider_msg]["_y"] - 22
                canvas.frame_divider(left, y_divider, right, divider_label,
                                     size=9)

        # 注释
        for note in self.notes:
            cx = xs[note["part"]]
            y_note = self.messages[note["after"]]["_y"] + 18
            x_note = cx + 14 if note.get("side", "right") == "right" \
                else cx - 14 - note["width"]
            canvas.note(x_note, y_note, note["width"], note["_h"],
                        note["text"])

        self.canvas = canvas
        return canvas

    def save(self, png_path, drawio_path, name, scale=3.0):
        canvas = self.build()
        return canvas.save(png_path, drawio_path, name, scale=scale)


class ObjectDiagram:
    """对象图：对象框（下划线名称 + 属性值）与对象之间的链接。"""

    def __init__(self, title, subtitle=""):
        self.title = title
        self.subtitle = subtitle
        self.objects = []   # {key, x, y, w, name, attributes}
        self.links = []     # {a, b, label, dashed, arrow, route}
        self.notes = []

    def object(self, key, x, y, name, attributes, width=250):
        self.objects.append(dict(key=key, x=x, y=y, w=width, name=name,
                                 attributes=attributes))
        return key

    def link(self, a, b, label="", dashed=False, arrow=None, route=None,
             label_side="middle", path=None):
        self.links.append(dict(a=a, b=b, label=label, dashed=dashed,
                               arrow=arrow, route=route,
                               label_side=label_side, path=path))

    def note(self, x, y, w, h, text):
        self.notes.append(dict(x=x, y=y, w=w, h=h, text=text))

    def _by_key(self, key):
        for item in self.objects:
            if item["key"] == key:
                return item
        raise KeyError(key)

    def _height(self, item):
        return 30 + 17 * len(item["attributes"]) + 8

    def build(self):
        max_x = max(item["x"] + item["w"] for item in self.objects)
        max_y = max(item["y"] + self._height(item) for item in self.objects)
        for note in self.notes:
            max_x = max(max_x, note["x"] + note["w"])
            max_y = max(max_y, note["y"] + note["h"])
        canvas = Canvas(max_x + 40, max_y + 30)
        canvas.text(canvas.width / 2, 18, self.title, size=15, bold=True)
        if self.subtitle:
            canvas.text(canvas.width / 2, 38, self.subtitle, size=9.5,
                        color=MUTED)

        for item in self.objects:
            height = self._height(item)
            item["_h"] = height
            canvas.rect(item["x"], item["y"], item["w"], height,
                        fill=BOX_FILL, stroke=BOX_LINE, width=1.4,
                        rounded=False)
            canvas.line(item["x"], item["y"] + 30, item["x"] + item["w"],
                        item["y"] + 30, color=BOX_LINE, width=1.0)
            canvas.text(item["x"] + item["w"] / 2, item["y"] + 15,
                        item["name"], size=10.5, bold=True)
            # 对象名按 UML 规范加下划线
            name_width = text_width(item["name"], 10.5, True)
            canvas.line(item["x"] + item["w"] / 2 - name_width / 2,
                        item["y"] + 22.5,
                        item["x"] + item["w"] / 2 + name_width / 2,
                        item["y"] + 22.5, color=INK, width=0.9)
            for index, attribute in enumerate(item["attributes"]):
                canvas.text(item["x"] + 10, item["y"] + 30 + 9 + index * 17,
                            attribute, size=9, align="left")

        for link in self.links:
            a = self._by_key(link["a"])
            b = self._by_key(link["b"])
            points = link["path"] if link.get("path") else self._route(a, b, link)
            if link["dashed"]:
                for start, end in zip(points, points[1:]):
                    canvas.line(start[0], start[1], end[0], end[1],
                                color=INK, width=1.1, dashed=True)
            else:
                for start, end in zip(points, points[1:]):
                    canvas.line(start[0], start[1], end[0], end[1],
                                color=INK, width=1.1)
            if link["arrow"]:
                start, end = points[-2], points[-1]
                canvas.items.append(dict(kind="arrow", x1=start[0], y1=start[1],
                                         x2=end[0], y2=end[1],
                                         arrow=link["arrow"], color=INK,
                                         width=1.1, label="", font_size=9,
                                         label_shift=0))
            if link["label"]:
                # 把标签放在最长的一段上，避免压在折角或对象框上
                segments = list(zip(points, points[1:]))
                longest = max(
                    segments,
                    key=lambda pair: abs(pair[0][0] - pair[1][0]) +
                    abs(pair[0][1] - pair[1][1]))
                mid = ((longest[0][0] + longest[1][0]) / 2,
                       (longest[0][1] + longest[1][1]) / 2)
                self._link_label(canvas, mid, link)

        for note in self.notes:
            canvas.note(note["x"], note["y"], note["w"], note["h"], note["text"])

        self.canvas = canvas
        return canvas

    @staticmethod
    def _link_label(canvas, mid, link):
        label = link["label"]
        width = text_width(label, 9) + 10
        canvas.rect(mid[0] - width / 2, mid[1] - 8, width, 16, fill="white",
                    stroke="white", width=0.0)
        canvas.text(mid[0], mid[1], label, size=9, color=MUTED)

    @staticmethod
    def _route(a, b, link):
        ah, bh = ObjectDiagram._height(a), ObjectDiagram._height(b)
        acx, acy = a["x"] + a["w"] / 2, a["y"] + ah / 2
        bcx, bcy = b["x"] + b["w"] / 2, b["y"] + bh / 2
        horizontal = abs(bcx - acx) > abs(bcy - acy)
        if horizontal:
            if bcx > acx:
                start, end = (a["x"] + a["w"], acy), (b["x"], bcy)
            else:
                start, end = (a["x"], acy), (b["x"] + b["w"], bcy)
        else:
            if bcy > acy:
                start, end = (acx, a["y"] + ah), (bcx, b["y"])
            else:
                start, end = (acx, a["y"]), (bcx, b["y"] + bh)
        if link["route"] == "hvh":
            mid_x = (start[0] + end[0]) / 2
            return [start, (mid_x, start[1]), (mid_x, end[1]), end]
        if link["route"] == "vhv":
            mid_y = (start[1] + end[1]) / 2
            return [start, (start[0], mid_y), (end[0], mid_y), end]
        return [start, end]

    def save(self, png_path, drawio_path, name, scale=3.0):
        canvas = self.build()
        return canvas.save(png_path, drawio_path, name, scale=scale)
