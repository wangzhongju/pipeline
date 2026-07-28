import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog
import yaml
import os
import re
import subprocess

class CodecConfigApp:
    def __init__(self, master):
        self.master = master
        master.title("编解码管道配置工具")
        
        # 初始化配置存储字典
        self.decoder_configs = {}
        self.encoder_configs = {}
        
        # 主框架布局
        self.top_frame = tk.LabelFrame(master, text="解码配置", padx=5, pady=5)
        self.bottom_frame = tk.LabelFrame(master, text="编码配置", padx=5, pady=5)
        self.control_frame = tk.Frame(master)
        
        self.top_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        self.bottom_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)
        self.control_frame.pack(fill=tk.X, padx=10, pady=10)
        
        # 解码视频源选择窗口 (6x6)
        self.create_decoder_grid(self.top_frame)
        
        # 编码生成文件窗口 (4x4)
        self.create_encoder_grid(self.bottom_frame)
        
        # 控制按钮
        self.btn_save = tk.Button(self.control_frame, text="保存配置", command=self.save_all)
        self.btn_start = tk.Button(self.control_frame, text="启动pipeline", command=self.start_pipeline)
        self.btn_exit = tk.Button(self.control_frame, text="关闭", command=master.quit)
        
        self.btn_save.pack(side=tk.LEFT, padx=5)
        self.btn_start.pack(side=tk.LEFT, padx=5)
        self.btn_exit.pack(side=tk.RIGHT, padx=5)

    def create_decoder_grid(self, parent):
        # 创建6x6宫格
        self.decoder_buttons = []
        for i in range(36):
            row = i // 6
            col = i % 6
            btn = tk.Button(parent, text=self.load_decoder_config(i+1), 
                          command=lambda idx=i+1: self.set_decoder_source(idx))
            btn.grid(row=row, column=col, sticky="nsew", padx=2, pady=2)
            parent.grid_columnconfigure(col, weight=1)
            parent.grid_rowconfigure(row, weight=1)
            self.decoder_buttons.append(btn)

    def load_decoder_config(self, grid_index):
        # 读取YAML配置
        yaml_path = f"/opt/demo/pipeline/case/codec/config/EsAvDemux_{grid_index}.yaml"
        try:
            if not os.path.exists(yaml_path):
                return "未配置"
                
            with open(yaml_path) as f:
                config = yaml.safe_load(f) or {}
                if config.get('file'):
                    return os.path.basename(config['file'])
                elif config.get('url'):
                    url = config['url']
                    return url if len(url) <=15 else f"{url[:12]}..."
        except Exception as e:
            print(f"加载配置错误: {str(e)}")
            return "配置错误"

    def set_decoder_source(self, grid_index):
        # 文件选择对话框
        file_path = filedialog.askopenfilename(
            initialdir="/opt/demo/pipeline/video/",
            filetypes=[("视频文件", "*.mp4 *.h264 *.h265 *.264 *.265")])
        
        if file_path:
            self.update_decoder_display(grid_index, file_path)
        else:
            # 弹出URL输入对话框
            url = simpledialog.askstring("输入URL", "请输入视频URL:")
            if url:
                self.update_decoder_display(grid_index, url=url)

    def update_decoder_display(self, grid_index, file_path=None, url=None):
        # 更新按钮显示
        btn = self.decoder_buttons[grid_index-1]
        display_text = os.path.basename(file_path) if file_path else (url[:15]+"..." if url else "")
        btn.config(text=display_text)
        
        # 保存临时配置
        if file_path:
            self.decoder_configs[grid_index] = {'file': file_path}
        elif url:
            self.decoder_configs[grid_index] = {'url': url}

    def save_all(self):
        # 保存解码配置
        for grid_index, config in self.decoder_configs.items():
            yaml_path = f"/opt/demo/pipeline/case/codec/config/EsAvDemux_{grid_index}.yaml"
            
            # 保留原有未修改的配置
            try:
                with open(yaml_path) as f:
                    original = yaml.safe_load(f) or {}
            except FileNotFoundError:
                original = {}
            
            # 合并配置
            original.update(config)
            
            with open(yaml_path, 'w') as f:
                yaml.dump(original, f)
        
        # 保存编码配置
        for grid_index, filename in self.encoder_configs.items():
            self.update_script_file(grid_index, filename)
        
        messagebox.showinfo("保存成功", "配置已保存！")

    def create_encoder_grid(self, parent):
        # 创建4x4宫格
        self.encoder_buttons = []
        for i in range(16):
            row = i // 4
            col = i % 4
            btn_text = self.parse_script_file(i+1)
            btn = tk.Button(parent, text=btn_text, 
                           command=lambda idx=i+1: self.set_encoder_path(idx))
            btn.grid(row=row, column=col, sticky="nsew", padx=2, pady=2)
            parent.grid_columnconfigure(col, weight=1)
            parent.grid_rowconfigure(row, weight=1)
            self.encoder_buttons.append(btn)

    def parse_script_file(self, grid_index):
        # 解析shell脚本
        pattern = re.compile(rf"venc_{grid_index}\s+-path\s+EsVenc.yaml\s+-\s+!\s+EsFileSink\s+-name\s+(.*?)\s+-path")
        try:
            with open("/opt/demo/pipeline/case/codec/codec_pipeline.sh") as f:
                for line in f:
                    match = pattern.search(line)
                    if match:
                        return match.group(1)
        except Exception as e:
            print(f"解析脚本错误: {str(e)}")
        return "未配置"

    def set_encoder_path(self, grid_index):
        # 路径选择对话框
        default_dir = "/home/eswin/demo/pipeline/codec/"
        os.makedirs(default_dir, exist_ok=True)
        file_path = filedialog.asksaveasfilename(
            initialdir=default_dir,
            defaultextension=".h264",
            filetypes=[("视频文件", "*.h264")])
        
        if file_path:
            filename = os.path.basename(file_path)
            self.encoder_buttons[grid_index-1].config(text=filename)
            self.encoder_configs[grid_index] = filename

    def update_script_file(self, grid_index, filename):
        # 更新shell脚本
        pattern = re.compile(rf"(venc_{grid_index}\s+-path\s+EsVenc.yaml\s+-\s+!\s+EsFileSink\s+-name\s+)(.*?)(\s+-path)")
        try:
            with open("/opt/demo/pipeline/case/codec/codec_pipeline.sh", "r+") as f:
                content = f.read()
                new_content = pattern.sub(rf"\1{filename}\3", content)
                f.seek(0)
                f.write(new_content)
                f.truncate()
        except Exception as e:
            messagebox.showerror("保存错误", f"更新脚本文件失败: {str(e)}")

    def start_pipeline(self):
        """
        启动pipeline的逻辑：
        1. 切换到tty1（文本桌面模式）
        2. 进入目标路径
        3. 执行sudo ./codec_pipeline.sh
        """
        try:
            # 切换到tty1（文本桌面模式）
            subprocess.run(["sudo", "chvt", "1"], check=True)
            
            # 进入目标路径
            target_dir = "/opt/demo/pipeline/case/codec/"
            if not os.path.exists(target_dir):
                messagebox.showerror("路径错误", f"目标路径不存在: {target_dir}")
                return
            
            subprocess.run(
                ["sudo", "./codec_pipeline.sh"],
                cwd=target_dir,
                check=True
            )
            # # 执行sudo命令
            # command = "sudo ./codec_pipeline.sh"
            # subprocess.run(["sudo", "bash", "-c", f"cd {target_dir} && {command}"], check=True)
            
        except subprocess.CalledProcessError as e:
            messagebox.showerror("启动错误", f"启动pipeline失败: {str(e)}")
        except Exception as e:
            messagebox.showerror("未知错误", f"发生未知错误: {str(e)}")

if __name__ == "__main__":
    root = tk.Tk()
    # 设置窗口初始大小
    root.geometry("800x600")
    # 使网格单元格随窗口缩放
    for i in range(6): root.grid_rowconfigure(i, weight=1)
    for i in range(4): root.grid_rowconfigure(i, weight=1)
    app = CodecConfigApp(root)
    root.mainloop()