# 构建与实现

| 路径 | 作用 |
| --- | --- |
| `standalone/main/app_main.c` | 启动 BSP、挂载 SD、准备运行目录、启动游戏 |
| `standalone/main/silt_host.c` | 将设备的显示、文件、任务、同步和音频能力接入原有移植 |
| `standalone/main/silt_world.c` | 河口地形和资源分布 |
| `standalone/main/silt_controls.c` | 触摸映射、中文 HUD、工具面板和音频开关 |
| `standalone/main/silt_rules.c` | 引擎动作与生存规则、掉落拾取、昼夜和双槽存档的桥接 |
| `standalone/survival.c` | 库存、配方、采集时间、工具耐久、体力、伤害和规则序列化 |
| `src/main/Window_Holo.c` | 引擎帧合成与 LCD 分带呈现 |
| `standalone/main/silt_assets.c` | 从 Flash 打开默认纹理包、音效和配乐 |

渲染沿用 ESP32-S3 端口的定点软件光栅和 8 位深度缓冲。较大的静态数据、游戏任务栈与场景快照放入 PSRAM。LCD DMA 缓冲和深度缓冲使用内部 RAM。完整的静态资源从 Flash 读取，SD 卡保存游戏状态。

## 重新生成原创资源

仓库已包含编译读取的生成结果。改动素材时，从仓库根目录运行：

```sh
python standalone/assets/generate_pack.py
python standalone/audio/generate_audio.py
python standalone/assets/generate_silt_font.py /path/to/NotoSansCJKsc-Bold.otf
```

字体脚本需要支持 FreeType 的 Pillow。字体文件与许可见 [第三方署名](../THIRD_PARTY.md)。重新生成资源后，重新编译固件即可。

## 实板抓帧

开发时可用 `idf.py -DSILT_CAPTURE_SCREEN=ON build` 编译一次性抓帧版本。进入地图 45 帧后，串口输出 `SILT_FRAME 320 240`、240 行索引化十六进制 RGB565 像素与 `SILT_FRAME_END`。日常版使用 `-DSILT_CAPTURE_SCREEN=OFF`。抓帧会增加串口传输负载，抓帧期间的帧率只代表调试情形。
