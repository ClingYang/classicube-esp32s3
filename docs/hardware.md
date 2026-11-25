# 硬件与验证

本项目针对立创实战派 ESP32-S3 N16R8。固件选择 ESP32-S3、16 MB Flash、240 MHz 和 80 MHz Octal PSRAM。显示走 SPI3 硬件 DMA，SD 走 SDMMC 单线模式。下表是固件中 `standalone/main/bsp/bsp_pins.h` 的配置。

| 功能 | 引脚或通道 |
| --- | --- |
| 共用 I²C0 | SDA GPIO1，SCL GPIO2 |
| FT6336 触摸 | 共用 I²C0，7 位地址 0x38 |
| PCA9557 扩展器 | 共用 I²C0，地址 0x19 |
| ST7789 | MOSI GPIO40，CLK GPIO41，DC GPIO39，背光 GPIO42 |
| LCD 片选与功放 | PCA9557 P0 为 LCD 片选，P1 为功放使能 |
| SDMMC | CLK GPIO47，CMD GPIO48，D0 GPIO21 |
| ES8311 音频 | MCLK GPIO38，BCLK GPIO14，LRCK GPIO13，DOUT GPIO45 |
| BOOT | GPIO0，低电平按下 |

## 快速验证

1. 插入可用的 FAT SD 卡，开机应看到中文启动信息和河口地图。
2. 按住左下方向区移动，再点“存档”。串口应出现 `saved slot`。
3. 重启后，串口应出现 `restored slot`，角色回到保存的位置。
4. 右上角音乐和音效应均显示关闭。开启其中一项后，可分别用静止聆听或移动、采集验证对应声音。
5. 原点画面异常时，检查串口是否出现 `[silt.assets] built-in terrain pack loaded`。默认材质由固件提供。

设备使用 USB Serial/JTAG。端口号取决于主机，可用 `python -m serial.tools.list_ports` 查看。串口监视可用 `idf.py -p <端口> monitor`。此版本的实板取证采用串口日志和 RGB565 画面读取。
