# EasyTHS 桥接（跟单台 → 同花顺鼠键）

将 `data/orders/outbox` 中的委托转发到本机 [EasyTHS](../../third_party/easyths) REST 服务（默认 `http://127.0.0.1:7648`）。

## 准备

1. 安装并登录同花顺下单程序（`xiadan.exe`），按 EasyTHS 文档完成客户端设置。
2. 启动 EasyTHS 服务：

```powershell
.\scripts\start_easyths.ps1
```

3. 跟单台设置里将交易通道选为 **EasyTHS(同花顺鼠键)**，保存。
4. 启动桥接：

```powershell
.\scripts\start_easyths_bridge.ps1
```

## 流程

```
跟单台/网格/条件单  →  orders/outbox/*.json
         ↓
  easyths_bridge/worker.py
         ↓
  EasyTHS REST  →  同花顺 xiadan.exe
         ↓
  orders/done 或 orders/reject
```

## 设置项（`data/settings.json`）

| 字段 | 默认 | 说明 |
|------|------|------|
| `broker_channel` | `mouse_sim` | 选模拟鼠键时写 outbox（旧值 `easyths` 同样生效） |
| `easyths_host` | `127.0.0.1` | |
| `easyths_port` | `7648` | |
| `easyths_api_key` | 空 | 服务端开了 key 再填 |
| `easyths_account` | 空 | 多账户时填账户名 |

源码副本：`third_party/easyths`（MIT）。也可不依赖本地副本，直接 `uvx "easyths[server]"`。
