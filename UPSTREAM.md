# 上下游源码关系

## 来源链

| 层级 | 仓库和版本 | 在本仓库中的角色 |
| --- | --- | --- |
| 1. ClassiCube 上游 | [ClassiCube/ClassiCube · bb706fd](https://github.com/ClassiCube/ClassiCube/tree/bb706fd05ad6c338e68a17b4f2db45ef3a21663a) | 提供 C 语言游戏引擎，包括地图和区块网格、玩家控制、输入、纹理加载和游戏循环。提交号来自 HoloCubic 端口的上游说明。 |
| 2. HoloCubic ESP32-S3 端口 | [clocteck/minecraft-esp32s3 · 883f3a8](https://github.com/clocteck/minecraft-esp32s3/tree/883f3a802a8dd7d1413ffd62d8b9c0b77fb0149b) | 在 ClassiCube 上加入 ESP32-S3 平台代码、定点软件渲染、双核批量光栅、RGB565 分条显示和内存优化。该端口的发布形式是供 HoloCubic 宿主加载的模块。 |
| 3. 实战派独立移植 | [ClingYang/classicube-esp32s3](https://github.com/ClingYang/classicube-esp32s3) | 以第 2 层为直接上游。保留 ESP32-S3 渲染底座，通过实战派板级驱动构建可独立烧录的 ESP-IDF 应用。加入触摸、中文界面、原创建材与音频、生存玩法和 SD 卡存档。 |

## 从哪里看代码

- [`upstream/ClassiCube/src/`](upstream/ClassiCube/src/) 是随项目提供的引擎代码。它包含第 2 层的 ESP32-S3 补丁，以及本项目为独立启动、位图资源、生存输入、地图生成和 PSRAM 做的后续改动。要核查本项目在引擎里的改动，可以和第 2 层的同路径代码对比。
- [`src/main/`](src/main/) 沿用第 2 层的 Holo 平台、窗口呈现和音频模块。独立固件在这里挂接触摸与界面合成，并读取嵌入固件的原创资源。
- [`standalone/`](standalone/) 是实战派独立工程。板级驱动复用此前实机验证的 ST7789、FT6336、SDMMC 和 ES8311 实现。`silt_host.c` 接到平台接口，`silt_controls.c` 负责触摸与中文 HUD，`silt_world.c` 和 `silt_rules.c` 分别负责河口地图和生存集成，`survival.c` 实现库存、工具、配方和昼夜规则。
- 世界资源、原创建材和音效来自 [素材生成脚本](docs/development.md#重新生成原创资源)。[第三方许可与署名](THIRD_PARTY.md) 记录 ClassiCube 的 BSD-3-Clause、HoloCubic 端口的 GPL v3、字库的 OFL，以及其余组件说明。

本仓库以第 2 层的固定提交为移植起点。保留的源码和后续修改一起发布，方便使用者按现有版本重建同一份固件。
