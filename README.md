# noctalia-seatfix

修一个组合环境下的剪贴板 bug：**Noctalia 的剪贴板历史不更新（复制什么面板都不显示），但复制粘贴本身正常**。

根因不是剪贴板坏了，而是 Noctalia 把剪贴板监听的 data-control device 建在了错误的 `wl_seat` 上。

## 症状

- Noctalia 剪贴板面板里只有旧条目，时间停在某一天；
- 之后不管复制什么文本/图片，面板都不新增；
- 但选中旧条目粘贴、或者用 Ctrl+V 粘贴当前内容都正常；
- `~/.local/state/noctalia/clipboard/entries/` 里的文件 mtime 不再变化。

## 根因

niri 的 shorin fork 提供第二个输入 seat 用于自动化（`niri msg agent` 相关的那个），名字叫 **`niri-agent`**，并且它把**两个 `wl_seat` 全局都广告给普通客户端**：

```
$ WAYLAND_DEBUG=1 wl-paste -l 2>&1 | grep -E 'global.*wl_seat|wl_seat#[0-9]+\.name'
wl_registry#2.global(42, "wl_seat", 9)
wl_registry#2.global(43, "wl_seat", 9)
wl_seat#12.name("seat0")        # 真正的输入 seat
wl_seat#13.name("niri-agent")   # 自动化 seat，无剪贴板
```

Noctalia 5 的 `WaylandConnection`（`src/wayland/wayland_connection.cpp`）在收到 `wl_seat` global 时直接覆盖成员：

```cpp
if (interfaceName == wl_seat_interface.name) {
  m_seat = static_cast<wl_seat*>(wl_registry_bind(...));  // 后到的 seat 覆盖前一个
  ...
  bindClipboardService();   // 剪贴板 device 建在这个 seat 上
}
```

于是后注册的 `niri-agent` seat 成为 `m_seat`，剪贴板 data-control device 建在它上面。niri 按 seat 分发 selection 事件，`niri-agent` 上永远没有剪贴板，所以：

- 启动时只收到 `ext_data_control_device_v1.selection(nil)`，之后再无任何事件（`WAYLAND_DEBUG=1` 可复现）；
- 同一时刻另一个进程的 `wl-paste --watch` 能正常收到事件，证明 compositor 侧没问题。

同一原因还会让 `zwp_text_input_v3`、`zwp_virtual_keyboard_v1`、`ext_idle_notification_v1` 都绑到 `niri-agent` 上（`WAYLAND_DEBUG` 里可以看到）。

## 修复

用 `LD_PRELOAD` 拦截 `wl_proxy_add_listener`，对每个 `wl_display` 只放行**第一个** `wl_seat`（`seat0`），把后续 seat 的 global 事件丢掉。Noctalia 因此绑定 `seat0`，剪贴板历史恢复。

不改 Noctalia 二进制、不改 niri、不动系统文件，只影响被预加载的那个进程。

```bash
make install          # 安装到 ~/.local/lib 与 ~/.local/bin
```

然后把 niri 的自启改成包装脚本（`~/.config/niri/config.kdl`）：

```kdl
spawn-at-startup "~/.local/bin/noctalia-seatfix"
```

重启 Noctalia（`pkill -f '^/usr/bin/noctalia'` 后重新启动）即可。

## 验证

```bash
# 1) 过滤库生效：只应看到 seat0
LD_PRELOAD=~/.local/lib/noctalia-seat-filter.so WAYLAND_DEBUG=1 wl-paste -l 2>&1 | grep 'wl_seat.*\.name'

# 2) 剪贴板恢复：复制一段文字后条目数 +1、index.enc mtime 更新
ls ~/.local/state/noctalia/clipboard/entries | wc -l
stat -c %y ~/.local/state/noctalia/clipboard/index.enc
grep '\[clipboard\]' ~/.cache/noctalia/noctalia.log | tail -2
# 期望看到: clipboard history size=... entries=N latest_mime=...
```

## 回滚

```bash
make uninstall
sed -i 's|spawn-at-startup "~/.local/bin/noctalia-seatfix".*|spawn-at-startup "noctalia"|' ~/.config/niri/config.kdl
```

（`~/.config/niri/config.kdl.bak-seatfix-*` 是改动前的备份。）

## 上游问题

两处都值得报：

1. **niri (shorin fork)**：agent seat 应只在 `wayland-1-agent` socket / agent 客户端上广告，普通客户端不该看到 `niri-agent`。niri 二进制里已有 `hides_agent_seat`，说明本意是隐藏，但当前版本（`26.04-shorin v26.04-458-gc5c36de4`）对所有客户端都暴露。
2. **Noctalia**：多 `wl_seat` 时不应随意覆盖 `m_seat`；至少应跳过无法提供剪贴板的 seat，或按 `wl_seat.name` 选择 `seat0`。

上游修好后本仓库即可弃用。

## 限制

- 只保留每个 `wl_display` 的第一个 seat。多物理 seat 的极端场景（例如两台键盘各自 seat）下会丢后续 seat；桌面环境里通常只有一个 `seat0`。
- `LD_PRELOAD` 只对通过 `wl_proxy_add_listener` 注册的 listener 生效；Noctalia 5 自己实现 Wayland 协议，已验证生效。

## License

MIT
