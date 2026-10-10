# 跟单台产品能力清单

面向 A 股跟单 / 区间 / 预警的桌面产品（C++ `follow_desk` + WebView2 UI）。

## 核心模块（均已接 API + UI + 后台轮询）

| 模块 | 能力 |
|------|------|
| 跟单 | 东财/雪球组合跟单 |
| 区间交易 | 助手风网格：五档/最新/市价报价、底仓、最大持仓、网格匹配、成交确认；上下限动作；参数模板（可默认自动填入） |
| 预警交易 | 价格 / 涨跌幅 / 通达信 sign 文件；自动下单或仅提醒 |
| 止损止盈 | 固定价/成本%止损止盈；移动止盈（最高价回落%） |
| 条件单 | 价格 / 时间 / 当日涨跌幅；一次性委托 |
| 自选池 | 高低价提醒 |
| 闪电下单 | 快速限价 + 风控（跳过半自动确认） |
| T+0 | 可设基准价；市价即时成交；买卖数量/每日轮次/冷却/时段；跨日重置 |
| 公式下单 | 文件信号名触发（tdxtrader 模式） |
| 篮子下单 | 多腿数量/权重执行 |
| 打板 | 相对首次价涨幅阈值买入 |
| 多账户 | 账户登记与资金 |
| 资金曲线 | 交易日权益快照 |
| 风控 | 交易日/时段、单笔金额、日委托、日亏暂停、同码冷却、半自动确认（硬限制不可关零） |
| 免责声明 | 首次启动须勾选同意，本地持久化 |
| 赞助提醒 | 账户权益超门槛软提醒（不锁定） |
| 通达信预警 | 监听文件或目录；解析代码/信号名；`normalize_code` 含北交所 |
| 交易程序 | 水母式本地工作流：拖拽选股器/股票池/策略单连线；链式进池；预授权+现有风控下单 |
| 模拟鼠键 | 同花顺窗口：`third_party/easyths` + `tools/easyths_bridge` 扫 outbox 转发 REST |
| 日志 | 事件 / 订单目录 / 引擎 |
| 行情 | 腾讯 / 新浪 / 东财（失败自动降级） |

## 稳定性

- 产品监控线程独立运行，异常不拖垮进程
- JSON 写入前 `.bak` 备份
- 行情多源降级；`data/holidays.json` 可覆盖节假日
- 冒烟：`python tools/smoke_product.py`

## 数据文件

`data/alerts.json` `stoploss.json` `conditions.json` `watchlist.json` `risk.json` `risk_state.json` `events.json`  
`pending_confirms.json` `disclaimer.json` `workflows.json` `workflow_state.json`  
`t0.json` `baskets.json` `board.json` `formulas.json` `accounts.json` `equity_curve.json` `templates.json` `holidays.json`

## 仍可增强（非阻塞）

OCR 读券商资产、自动更新包、打新/逆回购、真·涨停板价库、持仓同步、资金曲线图表。
