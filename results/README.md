# 可审计原始结果

运行脚本先把大体积临时输出写到忽略版本控制的 `build/results/`。发布证据经核对后，
以不修改内容的 `.txt`/`.csv` 文件复制到本目录并进入 Git；对应报告记录来源命令、
日期、内核、源码提交和 SHA-256。`summary` 文件是可再生派生物，不能代替 raw 数据。

已归档：

- 一小时混合压力测试完整控制台记录；
- 静态分析 environment、summary 和每工具原始输出；
- GPIO/libgpiod 逐操作 latency CSV、CPU time 与统计摘要；
- mock 事件逐事件 latency CSV、CPU time 与摘要；
- 最终集成/硬件 smoke 和内核异常核查记录。

目录名包含运行时间。正式报告在 `docs/test-report-zsh.md` 和
`docs/performance-zsh.md` 中列出对应源码提交与 SHA-256；首次失败的调试日志和
可再生临时构建目录不进入本目录。
