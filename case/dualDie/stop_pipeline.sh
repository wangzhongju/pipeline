#!/bin/bash

# 查找包含 espl_ 的进程的 PID
pids=$(ps -ef | awk '$8 ~ /espl_/ {print $2}')
if [ -z "$pids" ]; then
    echo "未找到包含 espl_ 的进程。"
else
    echo "找到以下包含 espl_ 的进程：$pids，正在尝试终止..."
    for pid in $pids; do
        if kill "$pid" 2>/dev/null; then
            echo "成功终止进程 $pid"
        else
            echo "无法终止进程 $pid"
        fi
    done
fi