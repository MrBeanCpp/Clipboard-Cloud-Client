<div align="center">
  <img src="img/Clipboard.png" style="display: inline-block; vertical-align: middle;">
  <h1 style="display: inline-block; vertical-align: middle;">Clipboard-Cloud</h1>
</div>

Clipboard-Cloud 是一个支持💻`Windows` 和 📱`iOS`间共享的云剪贴板

### 目前支持的数据格式：

- 文本（Text）
- 图像（Image）

### 特色

- **随处可用**：提供了`Java (Springboot)`后端，用于广域网数据交换
- **隐私保护**：
  - 基于内存的数据模型：不会持久化用户数据
  - 定期清理剪切板数据：防止信息泄露
  - 每日动态变化的`ID`：避免私钥泄露 (`TODO`)
  - `SHA256`算法：有效防止哈希碰撞
  - `HTTPS`协议：拒绝中间人攻击
- **良好的人机交互**：
  - `Windows`端上传数据后，会在光标周围显示小红点，提示用户，并在网络故障时，显示为黑点
  - `Windows`端接收数据后，会通过气泡通知用户
  - `IOS`端上传与下载均有弹窗通知
  - 网络故障时，会改变图标颜色通知用户
- **清爽的用户体验**：
  - 无需安装`IOS App`，仅用快捷指令完成
  - 无需登录注册，设定好`UUID` + 个人标识码（`UserID`）后即可使用
    - *`UserID`用于防止小概率的`UUID`碰撞 & 提高安全性，可置空*
    - *`ID = SHA256(UUID + UserID)`，请务必确保`Windows`端与`IOS`端`UUID & UserID`一致*
- **高分屏支持**：`Qt`原生高`DPI`缩放方案
- **高效简洁的客户端实现**：`Qt C++`，性能无需多言

### 模型

- 采用[生产者 - 消费者]模型，数据被`Get`之后，会从云端删除

### 限制

- 目前仅支持单台`Windows` & `IOS`间数据共享

### to-do

- 未来将支持多设备共享剪贴板

## 使用方式



