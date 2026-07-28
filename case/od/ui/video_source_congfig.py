import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Pango
import os
import yaml
import subprocess

# 定义支持的视频格式
SUPPORTED_VIDEO_FORMATS = ('.mp4', '.h264', '.h265', '.264', '.265')

class VideoGridApp(Gtk.Window):
    def __init__(self):
        Gtk.Window.__init__(self, title="Pipeline 配置界面")
        self.set_default_size(800, 600)
        self.grid_size = 5  # 默认5x5宫格
        self.video_selections = {}  # 存储每个宫格选择的视频文件路径或 URL
        self.config_dir = "/opt/demo/pipeline/case/od/config/"
        self.video_dir = "/opt/demo/pipeline/video/"
        self.script_dir = "/opt/demo/pipeline/case/od/"

        # 创建主布局
        self.main_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=10)
        self.add(self.main_box)

        # 创建N宫格选择下拉菜单
        self.grid_selector = Gtk.ComboBoxText()
        for size in [25, 36, 49, 64]:
            self.grid_selector.append_text(f"{size}宫格 ({int(size**0.5)}x{int(size**0.5)})")
        self.grid_selector.set_active(0)
        self.grid_selector.connect("changed", self.on_grid_selector_changed)
        self.main_box.pack_start(self.grid_selector, False, False, 0)

        # 创建滚动窗口以容纳N宫格
        self.scrolled_window = Gtk.ScrolledWindow()
        self.scrolled_window.set_policy(Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC)
        self.grid = Gtk.Grid()
        self.grid.set_row_spacing(10)
        self.grid.set_column_spacing(10)
        self.scrolled_window.add(self.grid)
        self.main_box.pack_start(self.scrolled_window, True, True, 0)

        # 创建保存、启动和关闭按钮
        button_box = Gtk.Box(spacing=10)
        self.save_button = Gtk.Button(label="保存配置")
        self.save_button.connect("clicked", self.on_save_clicked)
        button_box.pack_start(self.save_button, True, True, 0)

        self.start_button = Gtk.Button(label="启动pipeline")
        self.start_button.connect("clicked", self.on_start_clicked)
        button_box.pack_start(self.start_button, True, True, 0)

        self.close_button = Gtk.Button(label="关闭")
        self.close_button.connect("clicked", self.on_close_clicked)
        button_box.pack_start(self.close_button, True, True, 0)

        self.main_box.pack_start(button_box, False, False, 0)

        # 初始化宫格
        self.create_grid_buttons()

    def create_grid_buttons(self):
        """根据当前的grid_size创建N宫格按钮"""
        for child in self.grid.get_children():
            self.grid.remove(child)  # 清空现有宫格

        for i in range(self.grid_size):
            for j in range(self.grid_size):
                grid_index = i * self.grid_size + j + 1
                label = Gtk.Label()
                label.set_line_wrap(True)  # 自动换行
                label.set_max_width_chars(15)  # 最大字符数限制
                label.set_ellipsize(Pango.EllipsizeMode.END)  # 超出部分省略号显示
                button = Gtk.Button()
                button.add(label)
                button.connect("clicked", self.on_grid_button_clicked, i, j)
                self.grid.attach(button, j, i, 1, 1)
                self.load_yaml_file(grid_index, label)

        # 强制刷新界面
        self.grid.show_all()

    def load_yaml_file(self, grid_index, label):
        """加载yaml文件并显示file或url字段内容"""
        yaml_path = os.path.join(self.config_dir, f"EsAvDemux_{grid_index}.yaml")
        if os.path.exists(yaml_path):
            try:
                with open(yaml_path, 'r') as f:
                    yaml_data = yaml.safe_load(f)
                    file_or_url = yaml_data.get('file') or yaml_data.get('url')
                    if file_or_url:
                        if 'url' in yaml_data:
                            label.set_text(file_or_url)  # 显示完整的 URL
                        else:
                            label.set_text(os.path.basename(file_or_url))
            except Exception as e:
                print(f"Error reading YAML file {yaml_path}: {e}")
                label.set_text("Error")

    def on_grid_button_clicked(self, button, i, j):
        """处理宫格按钮点击事件"""
        dialog = Gtk.Dialog(title="选择文件或输入 URL", parent=self)
        dialog.add_buttons(Gtk.STOCK_CANCEL, Gtk.ResponseType.CANCEL,
                           Gtk.STOCK_OK, Gtk.ResponseType.OK)

        # 文件选择器
        file_chooser = Gtk.FileChooserButton(title="选择视频文件")
        file_chooser.set_current_folder(self.video_dir)
        video_filter = Gtk.FileFilter()
        video_filter.set_name("视频文件")
        for fmt in SUPPORTED_VIDEO_FORMATS:
            video_filter.add_pattern(f"*{fmt}")
        file_chooser.add_filter(video_filter)

        # URL 输入框
        url_entry = Gtk.Entry()
        url_entry.set_placeholder_text("输入 URL 地址")

        # 布局
        content_area = dialog.get_content_area()
        content_area.add(Gtk.Label(label="选择文件或输入 URL："))
        content_area.add(file_chooser)
        content_area.add(url_entry)
        content_area.show_all()

        response = dialog.run()
        if response == Gtk.ResponseType.OK:
            selected_file = file_chooser.get_filename()
            entered_url = url_entry.get_text().strip()
            label = button.get_child()
            if selected_file:
                label.set_text(os.path.basename(selected_file))
                self.video_selections[(i, j)] = selected_file
            elif entered_url:
                label.set_text(entered_url)
                self.video_selections[(i, j)] = entered_url
        dialog.destroy()

    def on_grid_selector_changed(self, combo):
        """处理宫格选择变化"""
        text = combo.get_active_text()
        size = int(text.split('宫格')[0])
        self.grid_size = int(size ** 0.5)
        self.create_grid_buttons()  # 重新生成宫格

    def on_save_clicked(self, button):
        """处理保存按钮点击事件"""
        for (i, j), selected_value in self.video_selections.items():
            grid_index = i * self.grid_size + j + 1
            yaml_path = os.path.join(self.config_dir, f"EsAvDemux_{grid_index}.yaml")
            if os.path.exists(yaml_path):
                try:
                    with open(yaml_path, 'r') as f:
                        yaml_data = yaml.safe_load(f)
                        if isinstance(selected_value, str) and selected_value.startswith(('rtsp://', 'http://', 'https://')):
                            yaml_data['url'] = selected_value
                            if "file" in yaml_data:
                                del yaml_data["file"]
                        else:
                            yaml_data['file'] = selected_value
                            if "url" in yaml_data:
                                del yaml_data["url"]

                    with open(yaml_path, 'w') as f:
                        yaml.dump(yaml_data, f, allow_unicode=True)
                except Exception as e:
                    print(f"Error writing YAML file {yaml_path}: {e}")

        dialog = Gtk.MessageDialog(
            transient_for=self,
            flags=0,
            message_type=Gtk.MessageType.INFO,
            buttons=Gtk.ButtonsType.OK,
            text="配置已保存",
        )
        dialog.format_secondary_text("所有配置文件已更新")
        dialog.run()
        dialog.destroy()

    def on_start_clicked(self, button):
        """处理启动按钮点击事件"""
        try:
            # 切换到文本模式
            subprocess.run(["sudo", "chvt", "1"], check=True)
            # 进入指定路径并执行脚本
            subprocess.run(
                ["sudo", "./od_pipeline.sh"],
                cwd=self.script_dir,
                check=True
            )
        except subprocess.CalledProcessError as e:
            print(f"Error during startup: {e}")

    def on_close_clicked(self, button):
        """处理关闭按钮点击事件"""
        Gtk.main_quit()

def main():
    win = VideoGridApp()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()