#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import time
import threading
import random
import sys
from tkinter import *
from tkinter import ttk
from pythonosc import udp_client
from watchdog.observers import Observer
from watchdog.events import FileSystemEventHandler
try:
    from pynput import keyboard
    from pynput.keyboard import Key
    PYNPUT_AVAILABLE = True
except ImportError:
    PYNPUT_AVAILABLE = False
    print("警告: pynput模块未安装，快捷键功能将不可用")

try:
    from PIL import Image, ImageTk, ImageDraw
    import pystray
    PIL_AVAILABLE = True
    PYSTRAY_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False
    PYSTRAY_AVAILABLE = False
    print("警告: PIL或pystray模块未安装，任务栏图标功能将不可用")

def resource_path(relative_path):
    """获取资源文件打包后的绝对路径（兼容PyInstaller）"""
    if hasattr(sys, '_MEIPASS'):
        return os.path.join(sys._MEIPASS, relative_path)
    return os.path.join(os.path.abspath("."), relative_path)

class VRChatLogHandler(FileSystemEventHandler):
    def __init__(self, callback):
        self.callback = callback
        self.current_log = None
        self.last_check = 0
        self.lock = threading.Lock()
        self.update_log_file()

    def get_vrchat_log_dir(self):
        appdata = os.getenv('APPDATA', '')
        return os.path.normpath(os.path.join(
            appdata, r'..\LocalLow\VRChat\VRChat'
        ))

    def find_latest_log(self):
        log_dir = self.get_vrchat_log_dir()
        if not os.path.exists(log_dir):
            return None
            
        logs = [f for f in os.listdir(log_dir) 
               if f.startswith('output_log_') and f.endswith('.txt')]
        if not logs:
            return None
            
        latest = max(
            logs,
            key=lambda x: os.path.getmtime(os.path.join(log_dir, x))
        )
        return os.path.join(log_dir, latest)

    def update_log_file(self):
        new_log = self.find_latest_log()
        if new_log != self.current_log:
            print(f"检测到新日志文件: {new_log}")
            self.current_log = new_log
            self.file_position = 0
            return True
        return False

    def safe_read_file(self):
        if not self.current_log or not os.path.exists(self.current_log):
            return ''
            
        try:
            with open(self.current_log, 'r', encoding='utf-8', errors='ignore') as f:
                f.seek(0, 2)
                file_size = f.tell()
                
                if self.file_position > file_size:
                    self.file_position = 0
                    
                f.seek(self.file_position)
                content = f.read()
                self.file_position = f.tell()
                return content
        except Exception as e:
            print(f"读取日志失败: {str(e)}")
            return ''

    def check_logs(self):
        while True:
            time.sleep(1)
            if self.update_log_file():
                continue
                
            content = self.safe_read_file()
            if "SAVED DATA" in content:
                self.callback(content)

    def start_monitor(self):
        self.observer = Observer()
        self.observer.schedule(self, path=self.get_vrchat_log_dir(), recursive=False)
        self.observer.start()
        
        self.check_thread = threading.Thread(target=self.check_logs, daemon=True)
        self.check_thread.start()

class AutoFishingApp:
    # 定义版本号常量
    VERSION = "2.1.1"
    
    def __init__(self, root):
        self.root = root
        self.running = False
        self.current_action = "等待"
        self.protected = False
        self.last_cycle_end = 0
        self.timeout_timer = None
        self.osc_client = udp_client.SimpleUDPClient("127.0.0.1", 9000)
        
        # 参数变量
        self.cast_time_var = DoubleVar(value=0.2)  # 默认0.2秒
        self.rest_time_var = DoubleVar(value=0.5)  # 默认0.5秒
        self.timeout_limit_var = DoubleVar(value=1.0)  # 默认1.0分钟
        self.rest_enabled = BooleanVar(value=False)  # 是否关闭装桶检测，默认不关闭（即启用装桶检测）
        
        # 随机蓄力时间相关变量
        self.random_cast_enabled = BooleanVar(value=False)  # 是否启用随机蓄力时间
        self.random_cast_max_var = DoubleVar(value=1.0)  # 随机蓄力时间最大值
        
        # 任务栏图标相关
        self.tray_icon = None
        self.icon_colors = {
            "等待": "#808080",  # 灰色
            "开始抛竿": "#FFA500",  # 橙色
            "鱼竿蓄力中": "#FF4500",  # 红色
            "等待鱼上钩": "#00FF00",  # 绿色
            "收杆中": "#FFD700",  # 金色
            "等待鱼装桶": "#9370DB",  # 紫色
            "休息中": "#87CEEB",  # 天蓝色
            "超时收杆": "#FF6347",  # 番茄色
            "已停止": "#808080"  # 灰色
        }
        
        # 通知控制
        self.minimize_notified = False  # 是否已经显示过最小化通知
        
        self.setup_ui()
        self.setup_hotkeys()
        self.setup_tray_icon()
        self.log_handler = VRChatLogHandler(self.fish_on_hook)
        self.log_handler.start_monitor()
        self.send_click(False)

        self.first_cast = True

    def toggle(self):
        self.running = not self.running
        self.start_btn.config(text="停止" if self.running else "开始")
        if self.running:
            self.first_cast = True  # 重置首次抛竿标志
            self.current_action = "开始抛竿"
            self.stats['start_time'] = time.time()  # 记录启动时间
            self.update_status()
            self.update_stats()
            threading.Thread(target=self.perform_cast).start()
            # 启动统计更新线程
            threading.Thread(target=self.update_stats_loop, daemon=True).start()
        else:
            self.emergency_release()
            # 清空统计信息
            self.stats = {
                'reels': 0,
                'timeouts': 0,
                'bucket_success': 0,
                'start_time': None
            }
            self.update_stats()

    def emergency_release(self):
        self.send_click(False)
        self.current_action = "已停止"
        self.update_status()

    def setup_ui(self):
        self.root.title(f"自动钓鱼 v{self.VERSION}")
        self.root.geometry("500x700")  # 增加窗口宽度和高度
        self.root.resizable(False, False)
        
        # 设置窗口图标（使用自定义ico文件）
        try:
            ico_path = resource_path("ico.ico")
            if os.path.exists(ico_path):
                # 确保任务栏图标和窗口图标一致
                self.root.iconbitmap(default=ico_path)  # 设置任务栏图标
                self.root.iconbitmap(ico_path)  # 设置窗口图标
                
                # 在Windows上，还需要设置WM_CLASS属性来影响任务栏图标
                if os.name == 'nt':  # Windows系统
                    try:
                        # 获取窗口句柄并设置应用程序ID
                        import ctypes
                        myappid = f'VRChatAutoFishing.{self.VERSION}'  # 使用当前版本号
                        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)
                    except Exception as e:
                        print(f"设置应用ID失败: {e}")
                
                print(f"使用自定义图标: {ico_path}")
            elif PIL_AVAILABLE:
                # 如果没有ico文件，使用PIL生成的图标作为备选
                icon_image = self.create_icon_image(self.icon_colors["等待"])
                icon_photo = ImageTk.PhotoImage(icon_image)
                self.root.iconphoto(True, icon_photo)
                print("使用PIL生成的图标")
        except Exception as e:
            print(f"设置窗口图标失败: {e}")
        
        # 设置窗口最小化到任务栏
        self.root.protocol("WM_DELETE_WINDOW", self.minimize_to_tray)
        
        # 设置窗口始终置顶（可选，用户可以通过任务栏图标控制）
        # self.root.attributes('-topmost', True)
        
        # 主框架
        main_frame = Frame(self.root)
        main_frame.pack(fill=BOTH, expand=True, padx=10, pady=10)
        
        # 标题栏（包含标题、版本号和日期）
        title_frame = Frame(main_frame)
        title_frame.pack(fill=X, pady=(0, 15))
        
        # 主标题
        title_label = Label(title_frame, text="🎣 VRChat 自动钓鱼", 
                           font=("Arial", 16, "bold"))
        title_label.pack(side=LEFT, padx=(0, 10))
        
        # 版本号和更新日期（与标题同行，靠右对齐）
        update_date = "2025-07-03"
        version_label = Label(title_frame, text=f"v{self.VERSION} ({update_date})", 
                             font=("Arial", 9), fg="gray")
        version_label.pack(side=RIGHT, pady=5)
        
        # 参数设置框架
        params_frame = LabelFrame(main_frame, text="⚙️ 参数设置", padx=10, pady=10)
        params_frame.pack(fill=X, pady=(0, 10))
        
        # 蓄力时间滑块
        cast_frame = Frame(params_frame)
        cast_frame.pack(fill=X, pady=5)
        Label(cast_frame, text="蓄力时间:", width=10, anchor=W).pack(side=LEFT)
        self.cast_scale = Scale(cast_frame, from_=0.2, to=2.0, resolution=0.1,
                               orient=HORIZONTAL, variable=self.cast_time_var,
                               command=self.on_cast_time_change)
        self.cast_scale.pack(side=LEFT, fill=X, expand=True, padx=(5, 0))
        self.cast_label = Label(cast_frame, text="0.2秒", width=8)
        self.cast_label.pack(side=RIGHT, padx=(5, 0))
        
        # 休息时间设置
        rest_enable_frame = Frame(params_frame)
        rest_enable_frame.pack(fill=X, pady=3)
        self.rest_enabled_check = Checkbutton(rest_enable_frame, text="关闭装桶检测(直接使用休息时间)", 
                                             variable=self.rest_enabled,
                                             command=self.on_rest_enabled_toggle)
        self.rest_enabled_check.pack(side=LEFT)
        
        # 休息时间滑块
        rest_frame = Frame(params_frame)
        rest_frame.pack(fill=X, pady=5)
        Label(rest_frame, text="休息时间:", width=10, anchor=W).pack(side=LEFT)
        self.rest_scale = Scale(rest_frame, from_=0.1, to=10.0, resolution=0.1,
                               orient=HORIZONTAL, variable=self.rest_time_var,
                               command=self.on_rest_time_change, 
                               state=NORMAL)
        self.rest_scale.pack(side=LEFT, fill=X, expand=True, padx=(5, 0))
        self.rest_label = Label(rest_frame, text="0.5秒", width=8, fg="black")
        self.rest_label.pack(side=RIGHT, padx=(5, 0))
        
        # 超时时间滑块
        timeout_frame = Frame(params_frame)
        timeout_frame.pack(fill=X, pady=5)
        Label(timeout_frame, text="超时时间:", width=10, anchor=W).pack(side=LEFT)
        self.timeout_scale = Scale(timeout_frame, from_=0.5, to=15.0, resolution=0.5,
                                   orient=HORIZONTAL, variable=self.timeout_limit_var,
                                   command=self.on_timeout_change)
        self.timeout_scale.pack(side=LEFT, fill=X, expand=True, padx=(5, 0))
        self.timeout_label = Label(timeout_frame, text="1.0分钟", width=8)
        self.timeout_label.pack(side=RIGHT, padx=(5, 0))
        
        # 随机蓄力时间选项
        random_cast_frame = Frame(params_frame)
        random_cast_frame.pack(fill=X, pady=5)
        
        # 复选框
        self.random_cast_check = Checkbutton(random_cast_frame, text="随机蓄力时间", 
                                            variable=self.random_cast_enabled,
                                            command=self.on_random_cast_toggle)
        self.random_cast_check.pack(side=LEFT)
        
        # 最大值滑块
        random_max_frame = Frame(params_frame)
        random_max_frame.pack(fill=X, pady=5)
        Label(random_max_frame, text="随机最大值:", width=10, anchor=W).pack(side=LEFT)
        self.random_max_scale = Scale(random_max_frame, from_=0.3, to=2.0, resolution=0.1,
                                     orient=HORIZONTAL, variable=self.random_cast_max_var,
                                     command=self.on_random_max_change, state=DISABLED)
        self.random_max_scale.pack(side=LEFT, fill=X, expand=True, padx=(5, 0))
        self.random_max_label = Label(random_max_frame, text="1.0秒", width=8, fg="gray")
        self.random_max_label.pack(side=RIGHT, padx=(5, 0))
        
        # 控制框架
        control_frame = LabelFrame(main_frame, text="🎮 控制面板", padx=15, pady=15)
        control_frame.pack(fill=X, pady=(0, 15))
        
        # 开始/停止按钮
        self.start_btn = Button(control_frame, text="开始", command=self.toggle, 
                               width=20, height=2, font=("Arial", 12, "bold"))
        self.start_btn.pack(pady=10)
        
        # 状态显示
        status_frame = Frame(control_frame)
        status_frame.pack(fill=X, pady=5)
        Label(status_frame, text="状态:", font=("Arial", 10)).pack(side=LEFT)
        self.status_label = Label(status_frame, text="[等待开始]", 
                                 font=("Arial", 10, "bold"), fg="blue")
        self.status_label.pack(side=LEFT, padx=(5, 0))
        
        # 快捷键说明
        hotkey_frame = Frame(control_frame)
        hotkey_frame.pack(fill=X, pady=5)
        Label(hotkey_frame, text="快捷键:", font=("Arial", 9)).pack(side=LEFT)
        hotkey_text = Label(hotkey_frame, text="F4: 显示/隐藏窗口  F5: 开始钓鱼  F6: 停止钓鱼", 
                           font=("Arial", 9), fg="gray")
        hotkey_text.pack(side=LEFT, padx=(5, 0))
        
        # 统计信息框架
        stats_frame = LabelFrame(main_frame, text="📊 统计信息", padx=10, pady=10)
        stats_frame.pack(fill=X, pady=(0, 10))
        
        # 创建左右两列统计信息
        left_stats_frame = Frame(stats_frame)
        left_stats_frame.pack(side=LEFT, fill=X, expand=True)
        
        right_stats_frame = Frame(stats_frame)
        right_stats_frame.pack(side=RIGHT, fill=X, expand=True)
        
        # 统计标签
        self.stats_labels = {}
        stats_data = [
            ("收杆次数", "reels", "0"),
            ("装桶次数", "bucket_success", "0"),
            ("超时次数", "timeouts", "0"),
            ("运行时间", "runtime", "0秒")
        ]
        
        # 将统计信息分成左右两列显示
        half = len(stats_data) // 2 + len(stats_data) % 2
        for i, (name, key, default) in enumerate(stats_data):
            # 选择显示在左列还是右列
            parent_frame = left_stats_frame if i < half else right_stats_frame
            
            # 为每个项目创建一个框架
            item_frame = Frame(parent_frame)
            item_frame.pack(fill=X, pady=2)
            
            # 添加标签和值
            Label(item_frame, text=f"{name}:", width=12, anchor=W).pack(side=LEFT)
            self.stats_labels[key] = Label(item_frame, text=default, width=12, anchor=W)
            self.stats_labels[key].pack(side=LEFT, padx=(5, 0))
        
        # 初始化统计
        self.stats = {
            'reels': 0,
            'timeouts': 0,
            'bucket_success': 0,  # 成功装桶次数
            'start_time': None
        }
        
        # 版权信息
        bottom_frame = Frame(main_frame)
        bottom_frame.pack(side=BOTTOM, fill=X, pady=(10, 0))
        
        copyright_label = Label(bottom_frame, text="[laomo]", 
                               font=("Arial", 8), fg="gray")
        copyright_label.pack(side=LEFT)

    def on_cast_time_change(self, value):
        """蓄力时间改变回调"""
        self.cast_label.config(text=f"{float(value):.1f}秒")

    def on_rest_time_change(self, value):
        """休息时间改变回调"""
        self.rest_label.config(text=f"{float(value):.1f}秒")

    def on_timeout_change(self, value):
        """超时时间改变回调"""
        self.timeout_label.config(text=f"{float(value):.1f}分钟")

    def on_random_cast_toggle(self):
        """随机蓄力时间选项改变回调"""
        if self.random_cast_enabled.get():
            self.random_max_scale.config(state=NORMAL)
        else:
            self.random_max_scale.config(state=DISABLED)

    def on_rest_enabled_toggle(self):
        """休息时间启用/禁用回调"""
        # 无论是否启用"关闭装桶检测"，休息时间滑块都应该保持可用状态
        # 因为两种模式都会使用休息时间
        pass

    def on_random_max_change(self, value):
        """随机最大值改变回调"""
        self.random_max_label.config(text=f"{float(value):.1f}秒")

    def setup_hotkeys(self):
        """设置快捷键"""
        if not PYNPUT_AVAILABLE:
            return
            
        def on_press(key):
            try:
                if key == Key.f4:
                    self.root.after(0, self.show_window_from_hotkey)
                elif key == Key.f5:
                    self.root.after(0, self.start_fishing)
                elif key == Key.f6:
                    self.root.after(0, self.stop_fishing)
            except AttributeError:
                pass

        self.keyboard_listener = keyboard.Listener(on_press=on_press)
        self.keyboard_listener.start()

    def show_window_from_hotkey(self):
        """从快捷键显示窗口"""
        # 如果窗口已隐藏，则显示
        if not self.root.winfo_viewable():
            self.show_window()
        else:
            # 如果窗口已显示，则隐藏
            self.minimize_to_tray()

    def setup_tray_icon(self):
        """设置任务栏图标"""
        if not PIL_AVAILABLE or not PYSTRAY_AVAILABLE:
            return
            
        try:
            # 创建任务栏图标
            self.create_tray_icon()
        except Exception as e:
            print(f"设置任务栏图标失败: {e}")

    def create_tray_icon(self):
        """创建任务栏图标"""
        # 创建图标图像
        icon_image = self.create_icon_image(self.icon_colors["等待"])
        
        # 创建菜单
        menu = pystray.Menu(
            pystray.MenuItem("显示窗口", self.show_window),
            pystray.MenuItem("开始/停止", self.toggle_from_tray),
            pystray.Menu.SEPARATOR,
            pystray.MenuItem("退出程序", self.quit_from_tray)
        )
        
        # 创建任务栏图标，提示文字显示当前状态和收杆次数
        self.tray_icon = pystray.Icon("auto_fishing", icon_image, f"自动钓鱼 v{self.VERSION} - 等待 | 收杆: 0 | 装桶: 0", menu)
        
        # 设置单击事件（显示/隐藏窗口）
        self.tray_icon.on_click = self.on_tray_click
        
        # 在后台线程中运行任务栏图标
        threading.Thread(target=self.tray_icon.run, daemon=True).start()

    def on_tray_click(self, icon, event):
        """任务栏图标单击事件"""
        # 左键单击显示/隐藏窗口
        if event.button == 1:  # 左键
            self.root.after(0, self.show_window_from_hotkey)

    def create_icon_image(self, color):
        """创建图标图像"""
        size = 64
        img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        
        # 绘制圆形图标
        draw.ellipse([8, 8, size-8, size-8], fill=color)
        
        return img

    def update_tray_icon_color(self):
        """更新任务栏图标颜色和提示文字"""
        if not PIL_AVAILABLE or not PYSTRAY_AVAILABLE or not self.tray_icon:
            return
            
        try:
            color = self.icon_colors.get(self.current_action, "#808080")
            new_icon_image = self.create_icon_image(color)
            self.tray_icon.icon = new_icon_image
            
            # 更新提示文字为当前状态、收杆次数和装桶次数
            reels_count = self.stats.get('reels', 0)
            bucket_count = self.stats.get('bucket_success', 0)
            self.tray_icon.title = f"自动钓鱼 v{self.VERSION} - {self.current_action} | 收杆: {reels_count} | 装桶: {bucket_count}"
        except Exception as e:
            print(f"更新任务栏图标失败: {e}")

    def show_window(self, icon=None, item=None):
        """从任务栏图标显示窗口"""
        # 显示窗口
        self.root.deiconify()
        
        # 将窗口置于前台
        self.root.lift()
        self.root.focus_force()
        
        # 如果窗口被最小化，恢复窗口
        if self.root.state() == 'iconic':
            self.root.state('normal')
        
        # 将窗口移到屏幕中央
        self.root.update_idletasks()
        x = (self.root.winfo_screenwidth() // 2) - (self.root.winfo_width() // 2)
        y = (self.root.winfo_screenheight() // 2) - (self.root.winfo_height() // 2)
        self.root.geometry(f"+{x}+{y}")

    def toggle_from_tray(self, icon=None, item=None):
        """从任务栏图标切换状态"""
        self.root.after(0, self.toggle)

    def quit_from_tray(self, icon=None, item=None):
        """从任务栏图标退出程序"""
        # 显示确认对话框
        from tkinter import messagebox
        result = messagebox.askyesno(
            "确认退出", 
            "确定要退出自动钓鱼程序吗？\n当前钓鱼状态将会停止。"
        )
        if result:
            self.root.after(0, self.on_close)

    def update_status(self):
        self.status_label.config(text=f"[{self.current_action}]")
        self.update_tray_icon_color()  # 更新任务栏图标颜色
        self.root.update()

    def update_stats(self):
        """更新统计信息"""
        if self.stats['start_time']:
            runtime = time.time() - self.stats['start_time']
            runtime_str = f"{runtime/60:.1f}分钟" if runtime > 60 else f"{runtime:.0f}秒"
            self.stats_labels['runtime'].config(text=runtime_str)
        
        # 更新所有统计数据
        for key in ['reels', 'bucket_success', 'timeouts']:
            if key in self.stats_labels and key in self.stats:
                self.stats_labels[key].config(text=str(self.stats[key]))
        
        # 同时更新任务栏提示信息中的收杆次数
        if PIL_AVAILABLE and PYSTRAY_AVAILABLE and self.tray_icon:
            self.update_tray_icon_color()
            
        self.root.update()

    def update_stats_loop(self):
        """定时更新统计信息"""
        while self.running:
            time.sleep(1)
            self.update_stats()

    def send_click(self, press):
        self.osc_client.send_message("/input/UseRight", 1 if press else 0)

    def get_param(self, var, default):
        try:
            value = var.get()
            # 蓄力时间最小0.2秒，最大2.0秒，其他参数最小0.1秒
            if var == self.cast_time_var:
                return max(0.2, min(2.0, value))
            else:
                return max(0.1, value)
        except:
            if var == self.cast_time_var:
                return max(0.2, min(2.0, default))
            else:
                return max(0.1, default)

    def get_cast_duration(self):
        """获取蓄力时间，支持随机蓄力时间"""
        if self.random_cast_enabled.get():
            # 随机蓄力时间：最小值0.2秒，最大值为用户设置的最大值
            min_cast = 0.2
            max_cast = self.get_param(self.random_cast_max_var, 1.0)
            return random.uniform(min_cast, max_cast)
        else:
            # 固定蓄力时间
            return self.get_param(self.cast_time_var, 0.2)

    def start_timeout_timer(self):
        if self.timeout_timer and self.timeout_timer.is_alive():
            self.timeout_timer.cancel()
        
        timeout = self.get_param(self.timeout_limit_var, 5) * 60
        self.timeout_timer = threading.Timer(timeout, self.handle_timeout)
        self.timeout_timer.start()

    def handle_timeout(self):
        if self.running and self.current_action == "等待鱼上钩":
            self.current_action = "超时收杆"
            self.update_status()
            self.stats['timeouts'] += 1
            self.update_stats()
            self.force_reel()

    def force_reel(self):
        if self.protected:
            return

        try:
            self.protected = True
            self.perform_reel(is_timeout=True)  # 标记为超时收杆
            
            # 添加休息时间，确保鱼竿恢复到可抛竿状态
            self.current_action = "休息中"
            self.update_status()
            time.sleep(1.0)  # 休息1.0秒，确保鱼竿恢复状态
            
            self.perform_cast()
        finally:
            self.protected = False

    def check_fish_pickup(self):
        """检查鱼是否已被钓起，通过检测日志中的'Fish Pickup attached to rod Toggles(True)'判断"""
        start_time = time.time()
        self.detected_time = None
        
        while time.time() - start_time < 30:
            content = self.log_handler.safe_read_file()
            
            if "Fish Pickup attached to rod Toggles(True)" in content:
                if not self.detected_time:
                    self.detected_time = time.time()
                    print("检测到鱼已上钩")
                    
            if self.detected_time and (time.time() - self.detected_time >= 2):
                return True
                
            time.sleep(0.5)
            
        print("未检测到鱼上钩")
        return False

    def perform_reel(self, is_timeout=False):
        self.current_action = "收杆中"
        self.update_status()
        self.stats['reels'] += 1
        self.update_stats()
        
        self.send_click(True)
        
        # 如果是超时收杆，不进行长时间等待
        if is_timeout:
            time.sleep(10.0)  # 超时情况下等待10秒
        else:
            success = self.check_fish_pickup()
            
            if success and self.detected_time:
                elapsed = time.time() - self.detected_time
                remaining_time = max(0, 2 - elapsed)
                if remaining_time > 0:
                    time.sleep(remaining_time)
        
        self.send_click(False)
        self.detected_time = None

    def perform_cast(self):
        # 无论是否启用休息时间，都不在这里进行休息逻辑
        # 休息逻辑已转移到fish_on_hook中处理
        if self.first_cast:
            self.first_cast = False  # 标记首次抛竿完成

        # 蓄力抛竿
        self.current_action = "鱼竿蓄力中"
        self.update_status()
        cast_duration = self.get_cast_duration()
        self.update_stats()
        
        self.send_click(True)
        time.sleep(cast_duration)
        self.send_click(False)

        # 抛竿后等待
        self.current_action = "等待鱼上钩"
        self.update_status()
        self.start_timeout_timer()
        time.sleep(3)

    def fish_on_hook(self, log_content=None):
        if not self.running or self.protected or time.time() - self.last_cycle_end < 2:
            return

        try:
            self.protected = True
            self.last_cycle_end = time.time()
            self.perform_reel()
            
            # 根据是否启用休息时间决定流程
            if self.rest_enabled.get():
                # 启用休息时间（关闭装桶检测）：直接休息，无需检测鱼装桶
                self.current_action = "休息中"
                self.update_status()
                rest_duration = self.get_param(self.rest_time_var, 0.5)
                time.sleep(max(0.1, rest_duration))
            else:
                # 未启用休息时间（启用装桶检测）：等待鱼装桶后再继续
                self.current_action = "等待鱼装桶"
                self.update_status()
                self.wait_for_fish_bucket()
                
                # 鱼装桶后也添加休息时间，使用用户设置的休息时间
                self.current_action = "休息中"
                self.update_status()
                rest_duration = self.get_param(self.rest_time_var, 0.5)
                time.sleep(max(0.1, rest_duration))
            
            self.perform_cast()
        finally:
            self.protected = False
            self.last_cycle_end = time.time()
            
    def wait_for_fish_bucket(self):
        """等待鱼装到桶里，通过检测日志中的"Attempt saving"来判断"""
        wait_start = time.time()
        
        while self.running:
            time.sleep(0.5)
            content = self.log_handler.safe_read_file()
            
            # 检查是否有鱼装桶信息，只需检测一次
            if "Attempt saving" in content:
                print("检测到鱼已装桶")
                self.stats['bucket_success'] += 1
                self.update_stats()
                break
            
            # 超过10秒还没有完成装桶，则超时处理（缩短超时时间）
            if time.time() - wait_start > 10:
                print("等待鱼装桶超时")
                break

    def on_close(self):
        self.emergency_release()
        try:
            if hasattr(self, 'timeout_timer') and self.timeout_timer:
                self.timeout_timer.cancel()
            
            if hasattr(self, 'keyboard_listener') and self.keyboard_listener:
                self.keyboard_listener.stop()
            
            if hasattr(self, 'tray_icon') and self.tray_icon:
                self.tray_icon.stop()
            
            # 修复observer引用错误
            if hasattr(self.log_handler, 'observer') and self.log_handler.observer.is_alive():
                self.log_handler.observer.stop()
                self.log_handler.observer.join(timeout=1)
            
            if hasattr(self.log_handler, 'check_thread'):
                self.log_handler.check_thread.join(timeout=0.5)
                
        except Exception as e:
            print(f"关闭时发生错误: {e}")
        finally:
            self.root.destroy()
            self.root.quit()

    def minimize_to_tray(self):
        """最小化到任务栏"""
        # 隐藏窗口
        self.root.withdraw()
        
        # 只在首次最小化时显示通知
        if not self.minimize_notified:
            # 如果任务栏图标可用，显示通知
            if PIL_AVAILABLE and PYSTRAY_AVAILABLE and self.tray_icon:
                try:
                    # 显示通知
                    self.tray_icon.notify(
                        title="自动钓鱼",
                        message="程序已最小化到任务栏，右键点击图标可显示窗口"
                    )
                except Exception as e:
                    print(f"显示通知失败: {e}")
            else:
                # 如果没有任务栏图标，显示消息框
                from tkinter import messagebox
                messagebox.showinfo(
                    "自动钓鱼", 
                    "程序已最小化到任务栏\n右键点击任务栏图标可显示窗口"
                )
            
            # 标记已显示过通知
            self.minimize_notified = True

    def start_fishing(self):
        """开始钓鱼"""
        if not self.running:
            self.toggle()

    def stop_fishing(self):
        """停止钓鱼"""
        if self.running:
            self.toggle()

def main():
    """主函数，用于程序入口"""
    root = Tk()
    app = AutoFishingApp(root)
    root.mainloop() 

if __name__ == "__main__":
    main() 