# 许可与署名

本项目新增的独立移植、原创玩法、资源和生成脚本采用 [GNU GPL v3.0](LICENSE)。

| 内容 | 来源与许可 |
| --- | --- |
| ESP32-S3 端口及 HoloCubic 平台代码 | [clocteck/minecraft-esp32s3](https://github.com/clocteck/minecraft-esp32s3/tree/883f3a802a8dd7d1413ffd62d8b9c0b77fb0149b)，端口新增部分由上游声明 GPL-3.0。源码位于 `src/main/`，引擎内的 ESP32-S3 补丁随引擎源码一同提供。 |
| ClassiCube | [ClassiCube/ClassiCube](https://github.com/ClassiCube/ClassiCube/tree/bb706fd05ad6c338e68a17b4f2db45ef3a21663a)，BSD-3-Clause。参见 [上游许可](upstream/ClassiCube/license.txt)、[署名](upstream/ClassiCube/credits.txt) 和 [上游说明](UPSTREAM.md)。上游列明的第三方组件保留各自许可。 |
| 中文位图字形 | 从 [Noto Sans CJK SC](https://github.com/notofonts/noto-cjk) 生成，SIL Open Font License 1.1。参见 [字库许可](standalone/assets/NotoSansCJK-OFL.txt)。 |
| 拉丁位图字形 | `standalone/main/bsp/font8x8_basic.h`，保留源文件中的公有领域声明和署名。 |
| 音频编解码依赖 | Espressif `esp_codec_dev` 1.6.2，由 ESP-IDF Component Manager 下载，许可见该组件发行包。 |

本仓库提供立创实战派独立固件所需的源文件和生成资源。HoloCubic 原应用包及其纹理、音频与演示文件属于上游项目，不在本仓库的发布清单中。
