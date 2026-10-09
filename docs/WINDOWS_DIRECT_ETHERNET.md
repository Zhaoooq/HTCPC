# Windows 与树莓派网线直连

## 需要的硬件

- 一根普通 RJ45 网线。树莓派 5 和现代 Windows 网卡通常支持自动翻转，不需要交叉线。
- 树莓派原有的独立电源。网线只负责通信，不能为树莓派供电。

## 1. 配置 Windows 有线网卡

1. 将网线连接到树莓派和 Windows 电脑。
2. 打开“设置 → 网络和 Internet → 高级网络设置 → 更多网络适配器选项”。
3. 右键正在使用的“以太网”，选择“属性”。
4. 双击“Internet 协议版本 4 (TCP/IPv4)”。
5. 选择“使用下面的 IP 地址”，填写：

```text
IP 地址：   192.168.50.1
子网掩码：  255.255.255.0
默认网关：  留空
DNS：       留空
```

Windows 显示“未识别的网络”或“无 Internet”属于正常现象，这条网线只建立本地数据链路。

## 2. 配置树莓派有线网卡

首次部署 CPC 时，管理员安装受限的 NetworkManager PolicyKit 权限并重启：

```bash
cd /home/pi/Desktop/HTCPC
sudo deployment/install-network-permissions.sh pi
sudo reboot
```

此后操作人员不需要打开桌面、终端或 SSH：

1. 启动 CPC，使用顶部页面选择器进入“通讯”。
2. 在“本机有线网络”选择“静态 IP”。
3. 设置 `192.168.50.2`、前缀 `/24`，网关和 DNS 保持未勾选（空）。
4. 点击“应用网络设置”，阅读断线提示后确认。
5. 等待页面显示应用成功，核对“当前实际 IP”为 `192.168.50.2`。

程序会创建或复用绑定到有线接口的 `HTCPC-ETH0` 专用 NetworkManager profile；不会假定系统连接名是 `Wired connection 1`。若应用或验证失败，程序会自动尝试恢复原 profile 和活动连接。HTCPC 主程序仍以普通用户身份运行。

需要 DHCP 时，在同一页面选择“DHCP 自动获取”再应用。如果现场没有 DHCP 服务器，页面显示“当前尚未获取 IPv4”是正常状态，不会影响 CPC 的测量与控制功能。

修改 IP 会断开已有的 Web 和工控机 TCP 连接，随后必须使用页面显示的新实际地址重新连接。

## 3. 编译并启动 CPC

工程新增了 Qt Network 依赖。在树莓派上编译：

```bash
cd /home/pi/Desktop/HTCPC
qmake HTCPC.pro
make -j2
./HTCPC
```

当前树莓派若尚未安装 Qt 5 开发工具，可先执行：

```bash
sudo apt-get install qtbase5-dev qt5-qmake
```

再重新运行 qmake。HTCPC 开机自启动配置仍会使用同一个 `HTCPC` 可执行文件。

## 4. 从 Windows 访问

以下端口为通讯页面的默认值。如果已在 CPC 本机修改并应用端口，请在以下命令、浏览器地址和工控机客户端中使用页面显示的实际端口。

先打开 PowerShell 检查链路：

```powershell
ping 192.168.50.2
Test-NetConnection 192.168.50.2 -Port 8080
Test-NetConnection 192.168.50.2 -Port 5000
```

然后使用 Edge 或 Chrome 打开：

```text
http://192.168.50.2:8080/
```

页面每秒获取一次最新快照，显示当前浓度、采集状态和最近 600 个有效数据点（约 10 分钟）。
点击页面右上角“保存数据”开始记录这一段看板数据，按钮变为“停止保存”后再次点击即可将本段 CSV 下载到 Windows 本地目录；如果浏览器没有弹出保存位置，请在 Edge/Chrome 的下载设置中开启“每次下载前询问保存位置”。
树莓派主程序内部最多保留 3600 个看板数据点，不会自动保存到磁盘。

工控机软件或测试脚本连接 `192.168.50.2:5000` 接收 CPC TCP Protocol V1.0：

```text
$CPC,<Version>,<Sequence>,<Concentration>,<Status>\r\n
$CPC,1,125,104.628,0\r\n
```

5000 端口只发送当前颗粒结果，不提供 HTTP 页面，也不发送 OPC 原始波形或 4000 点数据块。TCP 是字节流，Windows 客户端必须维护接收缓存并按 `\r\n` 提取完整帧，不能假设一次 `recv()` 就是一帧。

仓库提供 Python 3 测试客户端：

```powershell
python tools/test_tcp_client.py --host 192.168.50.2 --port 5000
```

## 常见问题

### 可以 ping 通，但端口 8080 或 5000 不通

- 确认新版本 `HTCPC` 已经编译并正在运行。
- 在树莓派执行 `ss -ltn | grep -E ':8080|:5000'`，应看到监听地址 `0.0.0.0:8080` 和 `0.0.0.0:5000`。
- 检查树莓派防火墙是否阻止 TCP 8080 或 TCP 5000。
- 检查是否已有其他程序占用了 8080 或 5000 端口。

### 页面打开，但没有曲线

只有 CPC 开始 OPC 采集、累计出第一组有效浓度后才会出现曲线。页面的“采集状态”可以区分
“连接正常但尚未采集”和“网络连接断开”。

### Windows 同时需要访问互联网

保留 Wi-Fi 用于互联网，把有线网卡专门用于 `192.168.50.0/24`。由于直连有线网卡没有填写
默认网关，一般不会抢占 Windows 的互联网默认路由。
