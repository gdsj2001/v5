# re v3 LVGL 产品 UI

目标：用 LVGL 等价承载 v2 UI 的页面、按钮、触摸和刷新底层，功能语义保持 v2 不变。UI 只负责显示、交互、状态呈现和命令发起；运动、坐标、回零、RTCP、MDI 和程序执行必须走 LinuxCNC/HAL/EtherCAT 原生链路。

当前阶段：

- 当前接手规则以仓库根目录 `AGENTS.md` 为准；`AI_并行任务看板.md` 已删除，不得读取或重建。
- 主页面、设置页、程序页、MDI 页和辅助页面已拆分到 `src/v3_page_*.c`。
- 旧 C/Python JSON Broker 命令中介已删除；UI 不再保留手写 JSON 命令拼包路线。

源码分块约束：

- `v3_ui_app.c` 只保留顶层 UI 创建和按钮分发。
- `v3_ui_keyboard.c` 承载虚拟键盘入口和键盘层。
- `v3_ui_command.c`、`v3_ui_events.c`、`v3_ui_state.c` 承载命令合同、事件日志和状态显示。
- `v3_product_main.c`、`v3_product_platform.c` 承载板端 framebuffer/input 运行入口。
- `v3_ui_popup.c`、`v3_ui_shell.c`、`v3_ui_widgets.c` 承载弹窗、切页和基础控件。
- `v3_page_*.c` 每个页面独立承载自己的渲染壳。
- 后续真实功能必须落在对应页面或功能模块中，不继续把业务堆回 `v3_ui_app.c`。

边界：

- 不修改 v2 文件。
- 不在 UI 层实现运动控制、插补、脉冲、坐标真值、回零语义或 RTCP 运动解算。
- 不恢复 `v3_product_command*` JSON Broker 命令中介层。
- 不把 mock/fixture 数据当成板端验收真值。
- 不提交 `bak/`、`evidence/`、`deploy_tmp/`、构建目录或板端运行快照。

构建说明：

仓库内已包含 `../third_party/lvgl-v8.3.11`，默认 CMake `all` 目标构建板端产品 UI，不依赖系统 LVGL 库：

```sh
mkdir -p build_arm_product
cd build_arm_product
cmake .. \
  -DRE_V3_BUILD_PRODUCT=ON \
  -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
  -DCMAKE_BUILD_TYPE=Release
cmake --build . --target re_v3_lvgl_product_ui -- -j2
```

最小 shell 目标 `re_v3_lvgl_shell` 只用于调试，默认关闭。确需构建时必须显式传入 `-DRE_V3_BUILD_SHELL=ON`，并提供可链接的系统 LVGL 库。
