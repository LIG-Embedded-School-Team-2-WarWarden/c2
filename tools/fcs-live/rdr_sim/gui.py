"""Desktop target editor and real RDR -> native ECS UDP demo."""

from dataclasses import replace
import json
import math
from pathlib import Path
import queue
import socket
import subprocess
import tempfile
import threading
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from .server import RdrServer
from .wire import Target
from .geometry import (Calibration, SENSOR, FIXED, fixed_target, input_target,
                       transmitted_targets)

ROOT = Path(__file__).resolve().parents[2]
BG, PANEL, FIELD = "#0b1220", "#121e30", "#192a40"
TEXT, MUTED, ACCENT, GREEN, RED = "#e5edf8", "#91a7c0", "#58c4ed", "#65ddb1", "#fa9191"


class App:
    def __init__(self, root):
        self.root = root
        root.title("RDR Dummy · 표적 시뮬레이터")
        root.geometry("1250x900")
        root.minsize(1100, 850)
        root.configure(bg=BG)
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.server = None
        self.process = None
        self.messages = queue.Queue()
        self.config_path = None
        self.targets = {1: Target(1, 3, .2, .1)}
        self.selection = 1
        self.dragging = False
        self.ecs_status = None
        self.extent = 5.0
        self.pan = self.tilt = 0.0
        self.input_frame = SENSOR
        self.calibration = Calibration()
        self.frame_var = tk.StringVar(value="센서 기준 (현재 시선)")
        self.bind_ip = tk.StringVar(value="127.0.0.1")
        self.bind_port = tk.StringVar(value="1001")
        self.ecs_ip = tk.StringVar(value="127.0.0.1")
        self.ecs_port = tk.StringVar(value="5000")
        self.rate = tk.StringVar(value="1 Hz")
        self.id_var = tk.StringVar(value="1")
        self.x_var = tk.StringVar(value="3.000")
        self.y_var = tk.StringVar(value="0.200")
        self.z_var = tk.StringVar(value="0.100")
        self.detected = tk.BooleanVar(value=True)
        self.pan_var = tk.StringVar(value="0.00")
        self.tilt_var = tk.StringVar(value="0.00")
        self.notice = tk.StringVar(value="좌표를 수정한 뒤 적용하거나, 지도에서 표적을 드래그하세요.")
        self.connection = tk.StringVar(value="●  대기 중")
        self.metrics = tk.StringVar(value="전송 0   ·   ECS 확인 0")
        self.ack_text = tk.StringVar(value="ECS 연결 전 · 수신 확인된 표적 없음")
        self.native_text = tk.StringVar(value="외부 ECS 또는 ‘로컬 ECS 실행’으로 연결할 수 있습니다.")
        self._style()
        self._build()
        self.refresh_targets()
        root.after(100, self.poll)

    def _style(self):
        s = ttk.Style()
        s.theme_use("clam")
        s.configure("TFrame", background=BG)
        s.configure("Card.TFrame", background=PANEL)
        s.configure("TLabel", background=BG, foreground=TEXT, font=("맑은 고딕", 10))
        s.configure("Card.TLabel", background=PANEL, foreground=TEXT)
        s.configure("Muted.TLabel", background=BG, foreground=MUTED, font=("맑은 고딕", 9))
        s.configure("Title.TLabel", font=("맑은 고딕", 21, "bold"), foreground=TEXT)
        s.configure("TButton", font=("맑은 고딕", 10), padding=(12, 8), background=FIELD,
                    foreground=TEXT, borderwidth=0)
        s.map("TButton", background=[("active", "#284260"), ("disabled", PANEL)],
              foreground=[("disabled", MUTED)])
        s.configure("Accent.TButton", background="#147d9e", foreground="white")
        s.map("Accent.TButton", background=[("active", "#2295b6")])
        s.configure("TEntry", fieldbackground=FIELD, foreground=TEXT, insertcolor=TEXT,
                    padding=6, borderwidth=0)
        s.configure("TCombobox", fieldbackground=FIELD, background=FIELD, foreground=TEXT,
                    arrowcolor=TEXT, padding=5)
        s.map("TCombobox", fieldbackground=[("readonly", FIELD)],
              foreground=[("readonly", TEXT)])
        s.configure("TCheckbutton", background=PANEL, foreground=TEXT)
        s.map("TCheckbutton", background=[("active", PANEL)])
        s.configure("Treeview", background=PANEL, fieldbackground=PANEL, foreground=TEXT,
                    rowheight=29, borderwidth=0, font=("맑은 고딕", 10))
        s.configure("Treeview.Heading", background=FIELD, foreground=MUTED,
                    font=("맑은 고딕", 9, "bold"), padding=6)
        s.map("Treeview", background=[("selected", "#24516d")])

    def _build(self):
        outer = ttk.Frame(self.root, padding=20)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(0, weight=1)
        outer.rowconfigure(3, weight=1)
        title = ttk.Frame(outer)
        title.grid(row=0, column=0, sticky="ew")
        ttk.Label(title, text="RDR  /  표적 시뮬레이터", style="Title.TLabel").pack(side="left")
        ttk.Label(title, textvariable=self.connection, foreground=GREEN).pack(side="right")
        ttk.Label(outer, text="입력: 센서/설치 기준 선택 · ECS 전송: 고정 설치 기준 · X 전방 / Y 좌측 / Z 상방 · 단위 m",
                  style="Muted.TLabel").grid(row=1, column=0, sticky="w", pady=(3, 12))

        net = ttk.Frame(outer)
        net.grid(row=2, column=0, sticky="ew", pady=(0, 12))
        for text, var, width in (("RDR IP", self.bind_ip, 12), ("포트", self.bind_port, 6),
                                 ("ECS IP", self.ecs_ip, 12), ("포트", self.ecs_port, 6)):
            ttk.Label(net, text=text).pack(side="left", padx=(0, 5))
            ttk.Entry(net, textvariable=var, width=width).pack(side="left", padx=(0, 10))
        self.server_button = ttk.Button(net, text="RDR 시작", command=self.toggle_server)
        self.server_button.pack(side="left", padx=(0, 7))
        self.demo_button = ttk.Button(net, text="로컬 ECS 실행", style="Accent.TButton",
                                      command=self.start_demo)
        self.demo_button.pack(side="right")
        self.rate_combo = ttk.Combobox(net, textvariable=self.rate,
                                       values=("1 Hz", "5 Hz", "10 Hz"),
                                       state="readonly", width=7)
        self.rate_combo.pack(side="right", padx=8)

        middle = ttk.Frame(outer)
        middle.grid(row=3, column=0, sticky="nsew")
        middle.columnconfigure(0, weight=3)
        middle.columnconfigure(1, weight=2, minsize=460)
        middle.rowconfigure(0, weight=1)
        chart = ttk.Frame(middle, style="Card.TFrame", padding=12)
        chart.grid(row=0, column=0, sticky="nsew", padx=(0, 12))
        ttk.Label(chart, text="변환된 표적  /  고정 설치 평면도", style="Card.TLabel",
                  font=("맑은 고딕", 12, "bold")).pack(anchor="w")
        self.canvas = tk.Canvas(chart, bg=PANEL, highlightthickness=0, height=260)
        self.canvas.pack(fill="both", expand=True, pady=(6, 0))
        self.canvas.bind("<Configure>", lambda e: self.draw())
        self.canvas.bind("<Button-1>", self.plot_down)
        self.canvas.bind("<B1-Motion>", self.plot_drag)
        self.canvas.bind("<ButtonRelease-1>", self.plot_up)
        self.canvas.bind("<Button-3>", self.plot_add)
        ttk.Label(chart, text="클릭·드래그: 선택 표적 이동  /  우클릭: 새 표적 추가",
                  style="Card.TLabel", foreground=MUTED, font=("맑은 고딕", 9)).pack(anchor="w")
        zoom = ttk.Frame(chart, style="Card.TFrame")
        zoom.pack(fill="x", pady=(5, 0))
        for value in (5, 10, 30):
            ttk.Button(zoom, text=f"±{value} m", command=lambda v=value: self.zoom(v)).pack(
                side="left", padx=(0, 5))

        edit = ttk.Frame(middle, style="Card.TFrame", padding=14)
        edit.grid(row=0, column=1, sticky="nsew")
        editor_heading = ttk.Frame(edit, style="Card.TFrame")
        editor_heading.pack(fill="x", pady=(0, 8))
        ttk.Label(editor_heading, text="입력 좌표계", style="Card.TLabel",
                  font=("맑은 고딕", 12, "bold")).pack(side="left")
        frame_combo = ttk.Combobox(editor_heading, textvariable=self.frame_var,
                                   values=("센서 기준 (현재 시선)", "고정 설치 기준"),
                                   state="readonly", width=22)
        frame_combo.pack(side="right")
        frame_combo.bind("<<ComboboxSelected>>", self.change_frame)
        entries = ttk.Frame(edit, style="Card.TFrame")
        entries.pack(fill="x")
        for col, (name, var) in enumerate((("ID", self.id_var), ("X (m)", self.x_var),
                                           ("Y (m)", self.y_var), ("Z (m)", self.z_var))):
            entries.columnconfigure(col, weight=1)
            ttk.Label(entries, text=name, style="Card.TLabel", foreground=MUTED).grid(
                row=0, column=col, sticky="w", padx=3)
            e = ttk.Entry(entries, textvariable=var, width=9)
            e.grid(row=1, column=col, sticky="ew", padx=3)
            e.bind("<Return>", lambda e: self.apply())
        buttons = ttk.Frame(edit, style="Card.TFrame")
        buttons.pack(fill="x", pady=10)
        ttk.Checkbutton(buttons, text="감지됨", variable=self.detected,
                        command=self.apply).pack(side="left")
        ttk.Button(buttons, text="적용 · 전송", style="Accent.TButton", command=self.apply).pack(
            side="right")
        ttk.Button(buttons, text="삭제", command=self.remove).pack(side="right", padx=5)
        ttk.Button(buttons, text="추가", command=self.add).pack(side="right")
        self.target_tree = self.tree(edit, height=3)
        self.target_tree.bind("<<TreeviewSelect>>", self.select)
        ttk.Label(edit, text="측정 시점 팬·틸트  /  +방위각: 좌측 · +고각: 위쪽",
                  style="Card.TLabel", foreground=MUTED, font=("맑은 고딕", 9)).pack(
            anchor="w", pady=(10, 4))
        pose = ttk.Frame(edit, style="Card.TFrame")
        pose.pack(fill="x")
        for label, var in (("방위각 °", self.pan_var), ("고각 °", self.tilt_var)):
            ttk.Label(pose, text=label, style="Card.TLabel").pack(side="left", padx=(0, 5))
            ttk.Entry(pose, textvariable=var, width=7).pack(side="left", padx=(0, 8))
        ttk.Button(pose, text="각도 적용", command=self.apply_pose).pack(side="right")
        ttk.Button(pose, text="축 보정", command=self.load_calibration).pack(side="right", padx=4)
        ttk.Label(edit, text="기본: 회전축·센서 원점 일치. 실제 장치에는 축 위치·장착 보정값이 필요합니다.",
                  style="Card.TLabel", foreground=MUTED, font=("맑은 고딕", 9)).pack(
            anchor="w", pady=(5, 0))

        received = ttk.Frame(outer, style="Card.TFrame", padding=12)
        received.grid(row=4, column=0, sticky="ew", pady=(12, 0))
        row = ttk.Frame(received, style="Card.TFrame")
        row.pack(fill="x")
        ttk.Label(row, text="ECS 수신 확인  /  고정 설치 좌표", style="Card.TLabel",
                  font=("맑은 고딕", 12, "bold")).pack(side="left")
        ttk.Label(row, textvariable=self.metrics, style="Card.TLabel", foreground=ACCENT).pack(
            side="right")
        self.received_tree = self.tree(received, height=2)
        ttk.Label(received, textvariable=self.ack_text, style="Card.TLabel",
                  foreground=GREEN).pack(anchor="w", pady=(4, 0))
        ttk.Label(received, textvariable=self.native_text, style="Card.TLabel",
                  foreground=MUTED, font=("맑은 고딕", 9)).pack(anchor="w")
        self.log_box = tk.Text(outer, height=2, bg=BG, fg=MUTED, relief="flat",
                               font=("Consolas", 9), state="disabled", wrap="word")
        self.log_box.grid(row=5, column=0, sticky="ew", pady=(8, 0))
        ttk.Label(outer, textvariable=self.notice, foreground=ACCENT, wraplength=1040).grid(
            row=6, column=0, sticky="w", pady=(4, 0))
        ttk.Label(outer, text="시연용 RDR–ECS 확장 초안 · UDP · 2바이트 좌표 / 1mm · 실제 장치 ICD 확정 전",
                  style="Muted.TLabel").grid(row=7, column=0, sticky="w", pady=(4, 0))

    @staticmethod
    def tree(parent, height):
        holder = ttk.Frame(parent, style="Card.TFrame")
        holder.pack(fill="x", pady=(7, 0))
        tree = ttk.Treeview(holder, columns=("id", "x", "y", "z", "state"),
                            show="headings", height=height, selectmode="browse")
        for key, label, width in (("id", "ID", 55), ("x", "X · m", 80), ("y", "Y · m", 80),
                                   ("z", "Z · m", 80), ("state", "상태", 80)):
            tree.heading(key, text=label)
            tree.column(key, width=width, minwidth=45, anchor="center")
        tree.pack(side="left", fill="x", expand=True)
        scrollbar = ttk.Scrollbar(holder, orient="vertical", command=tree.yview)
        scrollbar.pack(side="right", fill="y")
        tree.configure(yscrollcommand=scrollbar.set)
        return tree

    def error(self, error):
        self.notice.set(str(error))

    def refresh_targets(self):
        # Preserve the row selection and avoid refreshing entries during a user's edit.
        for item in self.target_tree.get_children():
            if int(item) not in self.targets:
                self.target_tree.delete(item)
        for t in self.targets.values():
            values = (t.id, f"{t.x:.3f}", f"{t.y:.3f}", f"{t.z:.3f}",
                      "감지" if t.detected else "미감지")
            if self.target_tree.exists(str(t.id)):
                self.target_tree.item(str(t.id), values=values)
            else:
                self.target_tree.insert("", "end", iid=str(t.id), values=values)
        self.draw()

    def select(self, event=None):
        selection = self.target_tree.selection()
        if selection and int(selection[0]) in self.targets:
            self.load(self.targets[int(selection[0])])

    def load(self, t):
        self.selection = t.id
        self.id_var.set(str(t.id))
        for var, value in ((self.x_var, t.x), (self.y_var, t.y), (self.z_var, t.z)):
            var.set(f"{value:.3f}")
        self.detected.set(t.detected)
        self.draw()

    def commit(self, t):
        t = t.quantized()
        if t.id not in self.targets and len(self.targets) >= 10:
            raise ValueError("표적은 최대 10개까지 추가할 수 있습니다.")
        transmitted_targets([t], self.input_frame, self.pan, self.tilt, self.calibration)
        if self.server:
            self.server.set_target(t)
        self.targets[t.id] = t
        self.selection = t.id
        self.refresh_targets()
        return t

    def apply(self):
        try:
            t = self.commit(Target(int(self.id_var.get()), float(self.x_var.get()),
                                   float(self.y_var.get()), float(self.z_var.get()),
                                   self.detected.get()))
            self.load(t)
            self.notice.set(f"표적 {t.id} 적용: ({t.x:.3f}, {t.y:.3f}, {t.z:.3f}) m · "
                            "연결·전송 허용 상태이면 변경 프레임을 즉시 보냅니다.")
        except (ValueError, OverflowError) as error:
            self.error(error)

    def add(self, x=3, y=0, fixed=False):
        try:
            id_ = next(i for i in range(1, 65536) if i not in self.targets)
            t = Target(id_, x, y, .1)
            if fixed:
                t = input_target(t, self.input_frame, self.pan, self.tilt, self.calibration)
            t = self.commit(t)
            self.target_tree.selection_set(str(id_))
            self.load(t)
        except ValueError as error:
            self.error(error)

    def remove(self):
        if self.selection not in self.targets:
            return
        if self.server:
            self.server.remove_target(self.selection)
        del self.targets[self.selection]
        self.selection = next(iter(self.targets), None)
        self.refresh_targets()
        if self.selection is not None:
            self.load(self.targets[self.selection])
        self.notice.set("표적 삭제 적용 · 빈 목록도 정상 프레임으로 ECS에 전송합니다.")

    def apply_pose(self):
        try:
            pan, tilt = float(self.pan_var.get()), float(self.tilt_var.get())
            from .wire import frame_body
            frame_body([], pan, tilt, 1000)
            pan, tilt = round(pan, 2), round(tilt, 2)
            transmitted_targets(self.targets.values(), self.input_frame, pan, tilt, self.calibration)
            if self.server:
                self.server.set_pose(pan, tilt)
            self.pan, self.tilt = pan, tilt
            self.pan_var.set(f"{pan:.2f}")
            self.tilt_var.set(f"{tilt:.2f}")
            self.draw()
            self.notice.set("각도 적용 · 센서 기준 입력은 회전·이동 변환 후 ECS로 전송합니다."
                            if self.input_frame == SENSOR else
                            "각도 적용 · 고정 설치 기준 입력은 표적의 고정 위치를 유지합니다.")
        except ValueError as error:
            self.error(error)

    def change_frame(self, event=None):
        frame = SENSOR if self.frame_var.get().startswith("센서") else FIXED
        try:
            converted = [input_target(fixed_target(t, self.input_frame, self.pan, self.tilt,
                                                   self.calibration), frame, self.pan, self.tilt,
                                      self.calibration).quantized() for t in self.targets.values()]
            transmitted_targets(converted, frame, self.pan, self.tilt, self.calibration)
            if self.server:
                self.server.set_input_frame(frame)
            self.targets = {t.id: t for t in converted}
            self.input_frame = frame
            self.refresh_targets()
            if self.selection in self.targets:
                self.load(self.targets[self.selection])
            self.notice.set("입력 좌표계를 변경했습니다. 기존 표적의 설치 기준 위치는 유지합니다.")
        except ValueError as error:
            self.frame_var.set("센서 기준 (현재 시선)" if self.input_frame == SENSOR else "고정 설치 기준")
            self.error(error)

    def load_calibration(self):
        path = filedialog.askopenfilename(parent=self.root, title="팬틸트·센서 보정 JSON",
                                          initialdir=ROOT / "configs",
                                          filetypes=[("JSON", "*.json")])
        if not path:
            return
        try:
            calibration = Calibration(**json.loads(Path(path).read_text(encoding="utf-8")))
            transmitted_targets(self.targets.values(), self.input_frame, self.pan, self.tilt,
                                calibration)
            if self.server:
                self.server.set_calibration(calibration)
            self.calibration = calibration
            self.draw()
            self.notice.set("축 보정 적용: " + Path(path).name)
        except (OSError, ValueError, TypeError) as error:
            self.error(error)

    def zoom(self, value):
        self.extent = value
        self.draw()

    def projection(self):
        w, h = self.canvas.winfo_width(), self.canvas.winfo_height()
        return w / 2, h / 2, max(1, min(w - 70, h - 60)) / (2 * self.extent)

    def draw(self):
        if not hasattr(self, "canvas"):
            return
        c = self.canvas
        c.delete("all")
        cx, cy, scale = self.projection()
        radius = self.extent * scale
        for factor in (.25, .5, .75, 1):
            r = radius * factor
            c.create_oval(cx-r, cy-r, cx+r, cy+r, outline="#263a51", dash=(3, 4))
        c.create_line(cx-radius, cy, cx+radius, cy, fill="#36536c")
        c.create_line(cx, cy+radius, cx, cy-radius, fill="#36536c", arrow="last")
        c.create_text(cx+8, cy-radius-12, text="+X 전방", anchor="w", fill=MUTED)
        c.create_text(cx-radius, cy+16, text="+Y 좌측", anchor="w", fill=MUTED)
        c.create_text(cx+radius-5, cy+16, text="−Y", anchor="e", fill=MUTED)
        c.create_polygon(cx, cy-8, cx-6, cy+6, cx+6, cy+6, fill=ACCENT)
        origin = self.calibration.to_fixed((0, 0, 0), self.pan, self.tilt)
        direction = self.calibration.to_fixed((1, 0, 0), self.pan, self.tilt)
        c.create_line(cx-origin[1]*scale, cy-origin[0]*scale,
                      cx-direction[1]*scale, cy-direction[0]*scale,
                      fill=ACCENT, width=3, arrow="last")
        c.create_text(12, 15, text=f"PAN {self.pan:.2f}°   TILT {self.tilt:.2f}°",
                      fill=ACCENT, anchor="w", font=("Consolas", 10))
        for input_t in self.targets.values():
            t = fixed_target(input_t, self.input_frame, self.pan, self.tilt, self.calibration)
            px, py = cx-t.y*scale, cy-t.x*scale
            if abs(t.x) > self.extent or abs(t.y) > self.extent:
                continue
            color = GREEN if t.detected else MUTED
            if t.id == self.selection:
                c.create_oval(px-14, py-14, px+14, py+14, outline=ACCENT, width=2)
            c.create_oval(px-6, py-6, px+6, py+6, fill=color if t.detected else PANEL,
                          outline=color, width=2)
            c.create_text(px+14, py-9, text=f"T{t.id}", fill=TEXT, anchor="w",
                          font=("Consolas", 11, "bold"))
            c.create_text(px+14, py+9, text=f"Z {t.z:.2f} m", fill=MUTED, anchor="w",
                          font=("Consolas", 9))

    def coordinates(self, event):
        cx, cy, scale = self.projection()
        return round((cy-event.y)/scale, 3), round((cx-event.x)/scale, 3)

    def plot_down(self, event):
        cx, cy, scale = self.projection()
        for t in self.targets.values():
            fixed = fixed_target(t, self.input_frame, self.pan, self.tilt, self.calibration)
            if math.hypot(event.x-(cx-fixed.y*scale), event.y-(cy-fixed.x*scale)) <= 18:
                self.load(t)
                self.target_tree.selection_set(str(t.id))
                break
        self.dragging = True
        self.plot_drag(event)

    def plot_drag(self, event):
        if self.dragging and self.selection in self.targets:
            x, y = self.coordinates(event)
            try:
                previous = fixed_target(self.targets[self.selection], self.input_frame,
                                        self.pan, self.tilt, self.calibration)
                fixed = replace(previous, x=x, y=y)
                t = self.commit(input_target(fixed, self.input_frame, self.pan, self.tilt,
                                              self.calibration))
                self.x_var.set(f"{t.x:.3f}")
                self.y_var.set(f"{t.y:.3f}")
                self.z_var.set(f"{t.z:.3f}")
            except ValueError as error:
                self.error(error)

    def plot_up(self, event):
        self.plot_drag(event)
        self.dragging = False

    def plot_add(self, event):
        self.add(*self.coordinates(event), fixed=True)

    def start_server(self):
        if self.server:
            return
        server = RdrServer((self.bind_ip.get(), int(self.bind_port.get())),
                           (self.ecs_ip.get(), int(self.ecs_port.get())))
        try:
            server.targets.clear()
            server.set_calibration(self.calibration)
            server.set_input_frame(self.input_frame)
            server.set_pose(self.pan, self.tilt)
            for t in self.targets.values():
                server.set_target(t)
            server.start()
        except Exception:
            server.close()
            raise
        self.server = server
        self.bind_port.set(str(server.address[1]))
        self.server_button.configure(text="RDR 중지")
        self.notice.set("RDR 수신 대기 중 · ECS 연결 요청과 전송 허용을 기다립니다.")

    def toggle_server(self):
        try:
            if self.server:
                self.server.close()
                self.server = None
                self.server_button.configure(text="RDR 시작")
            else:
                self.start_server()
        except (ValueError, OSError) as error:
            self.error(error)

    def start_demo(self):
        messagebox.showinfo("Live ECS", "scripts/start_live_fcs_test.ps1로 ECS를 실행하세요.")

    def poll(self):
        while not self.messages.empty():
            line = self.messages.get_nowait()
            try:
                status = json.loads(line)
                if status.get("ready"):
                    self.ecs_ip.set("127.0.0.1")
                    self.ecs_port.set(str(status["port"]))
                    if self.server:
                        with self.server.lock:
                            self.server.ecs = ("127.0.0.1", status["port"])
                    self.native_text.set(f"실제 C++ ECS 실행 중 · 127.0.0.1:{status['port']}")
                elif "observation" in status:
                    self.ecs_status = status
                    self.native_text.set(f"C++ ECS 조회: {status['observation']} · "
                                         f"프레임 {status.get('frame')} · "
                                         f"표적 {len(status.get('objects', []))}개")
                elif "rejected" in status:
                    self.notice.set(status["rejected"])
            except (ValueError, AttributeError):
                if line:
                    self.notice.set(line)
        if self.process:
            if self.process.poll() is None:
                try:
                    self.process.stdin.write("status\n")
                    self.process.stdin.flush()
                except (BrokenPipeError, OSError):
                    pass
            else:
                self.demo_button.configure(text="로컬 ECS 실행")
                self.rate_combo.configure(state="readonly")
                self.native_text.set(f"로컬 ECS 종료 · 코드 {self.process.returncode}")
        if self.server:
            s = self.server.snapshot()
            ack_current = s["ack_age"] is not None and s["ack_age"] <= max(1.5, s["period_ms"] / 1000 * 1.5)
            self.connection.set("●  ECS 수신 확인" if ack_current else
                                "●  연결됨 · 확인 대기" if s["connected"] else "●  ECS 연결 대기")
            self.metrics.set(f"전송 {s['sent']}   ·   ECS 확인 {s['acked']}   ·   오류 {s['errors']}")
            if s["last_ack_frame"]:
                self.ack_text.set(f"ECS 적용 완료: 프레임 {s['last_ack_frame']} · "
                                  f"표적 {len(s['confirmed'])}개 · " +
                                  (f"{s['ack_age']:.1f}초 전" if ack_current else "오래된 수신 결과"))
            else:
                self.ack_text.set("아직 ECS가 적용을 확인한 프레임이 없습니다.")
            self.received_tree.delete(*self.received_tree.get_children())
            # This table is populated only by an ACK after the real C++ ECS applied the frame.
            if ack_current:
                for t in s["confirmed"]:
                    self.received_tree.insert("", "end", values=(t.id, f"{t.x:.3f}", f"{t.y:.3f}",
                                                                f"{t.z:.3f}", "ECS 확인"))
            self.log_box.configure(state="normal")
            self.log_box.delete("1.0", "end")
            self.log_box.insert("end", "\n".join(s["logs"][-3:]))
            self.log_box.configure(state="disabled")
        else:
            self.connection.set("●  RDR 중지됨")
            self.received_tree.delete(*self.received_tree.get_children())
            self.ack_text.set("RDR 중지됨 · 현재 수신 확인 없음")
        self.root.after(250, self.poll)

    def stop_demo(self):
        if self.process:
            if self.process.poll() is None:
                try:
                    self.process.stdin.write("quit\n")
                    self.process.stdin.flush()
                    self.process.wait(timeout=4)
                except (OSError, subprocess.TimeoutExpired):
                    self.process.terminate()
                    self.process.wait(timeout=2)
            self.process.stdin.close()
            self.process.stdout.close()
            self.process = None
        self.demo_button.configure(text="로컬 ECS 실행")
        self.rate_combo.configure(state="readonly")
        self.native_text.set("로컬 ECS 중지됨")

    def close(self):
        self.stop_demo()
        if self.server:
            self.server.close()
        if self.config_path:
            self.config_path.unlink(missing_ok=True)
        self.root.destroy()


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()
