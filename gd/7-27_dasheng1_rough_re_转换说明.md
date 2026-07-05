# 7-27_dasheng1_rough_re.ngc 转换说明

## 来源

- 原始文件：`D:\re\gd\7-27大圣1开粗.tap`
- 转换输出：
  - `D:\re\gd\7-27_dasheng1_rough_re.ngc`
  - `D:\re\phase0_native\bus5\nc\7-27_dasheng1_rough_re.ngc`
  - 板端：`/opt/8ax/phase0_bus5/nc/7-27_dasheng1_rough_re.ngc`

## 转换规则

1. 删除首尾 `%` 包装行。
2. 增加 RE/8ax 安全头：
   - `M129`
   - `G4 P0.050`
   - `G21 G17 G90 G40 G49 G80 G94 G54`
   - `G64 P0.100`
3. 原始文件注释写有 `Set Mult-Axis ON`，且 `G96/G97` 只出现在多轴开关位置；因此按源后处理语义转换：
   - `G96` -> `M128`
   - `G97` -> `M129`
4. `M05` 规范化为 `M5`。
5. XYZAC 坐标值原样保留，不对 C 轴连续角度做 360 取模，避免改变实际刀路。
6. 未自动插入 `G43 H1`，因为原始程序未说明坐标是刀尖点还是刀柄点，不能擅自增加刀长偏置。

## 扫描结果

- 源文件行数：89338
- 输出文件行数：89346
- 进给段：89322
- G0 段：4，RE 轨迹解析会隐藏 G0，不画入黄色加工轨迹
- 可执行代码中 `G96/G97` 数量：0
- RTCP 范围：`M128` 到结尾前 `M129`

轴范围：

```text
X: -11.931 .. 24.428
Y: -11.053 .. 14.475
Z: -43.989 .. 0.000
A: -90.000 .. 0.000
C: -90.000 .. 159030.000
```

注意：C 轴是大角度连续旋转刀路。实际运行前必须确认 RE/8ax 的 C 轴限位、等效角度、回零和旋转轴策略允许该连续角度；本次只保证格式和语义转换，不把连续 C 轴刀路改写成取模显示。

## 验证

1. 本地 RE 解析通过：
   - `schema=re.toolpath_cache.v2`
   - `program_lines=89346`
   - `feed_segments=89322`
   - `rapid_segments=4`
   - `yellow_g0_segments=0`
2. 文件已同步到 VM truth source 和板端 nc 目录。
3. 板端 LinuxCNC open-only 验证通过：
   - 动作：只执行 `program_open`，没有 `AUTO_RUN`
   - `stat.file=/opt/8ax/phase0_bus5/nc/7-27_dasheng1_rough_re.ngc`
   - LinuxCNC error channel 为空
