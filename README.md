# STM32 MAX30102 健康监测系统

基于 STM32F103C8T6 + FreeRTOS 开发的便携式心率血氧监测系统，支持 WiFi 无线数据透传与运动状态识别。

## ✨ 功能特性

- ✅ **心率检测**：25Hz 采样，4s 计算周期，范围 60-100bpm
- ✅ **血氧饱和度检测**：基于 PPG 信号处理，范围 95%-100%
- ✅ **运动状态识别**：5 级运动分级（静止/微动/步行/跑步/剧烈）
- ✅ **WiFi 无线透传**：ESP8266 AT 指令透传，TCP 数据上传
- ✅ **OLED 实时显示**：128x64 OLED 显示心率、血氧数据
- ✅ **自定义二进制协议**：帧格式 `[0xAA | TYPE | LEN | SEQ | PAYLOAD | CHECK | 0x55]`

## 📦 硬件平台

| 组件 | 型号 | 说明 |
|------|------|------|
| MCU | STM32F103C8T6 | Cortex-M3，72MHz，64KB Flash，20KB SRAM |
| 心率血氧传感器 | MAX30102 | 光电容积脉搏波传感器 |
| 六轴传感器 | MPU6050 | 3轴加速度 + 3轴陀螺仪 |
| WiFi 模块 | ESP8266 | 串口转 WiFi，AT 指令控制 |
| 显示 | OLED 128x64 | I2C 接口 |
| 调试器 | ST-Link | SWD 接口 |

## 🔧 软件架构

### FreeRTOS 任务架构

| 任务 | 优先级 | 栈大小 | 功能 |
|------|--------|--------|------|
| SensorTask | High | 384 | MAX30102 传感器数据采集 (25Hz) |
| AlgoTask | Med-High | 512 | 心率/血氧算法计算 (4s) |
| TxTask | Med | 256 | 协议帧发送 + 运动检测 (1Hz) |

### 任务间通信

- **Queue**：传感器数据队列、心率结果队列
- **Mutex**：I2C 总线互斥锁、OLED 显示互斥锁

### 通信协议

帧格式：`[0xAA | TYPE(1B) | LEN(1B) | SEQ(1B) | PAYLOAD(NB) | CHECK(1B) | 0x55]`

支持的帧类型：
- `0x01`：MPU6050 六轴数据
- `0x02`：心率数据
- `0x03`：运动检测分数
- `0x04`：文本消息
- `0xFF`：心跳帧

## 📁 目录结构

```
.
├── FreeRTOS/          # FreeRTOS 实时操作系统
│   ├── inc/           # 头文件
│   └── src/           # 源文件
├── Library/           # STM32 标准库 (STM32F10x_StdPeriph_Driver)
├── SYSTEM/            # 系统核心模块
│   ├── Delay.c/h      # 延时函数
│   ├── usart.c/h      # 串口驱动
│   └── sys.c/h        # 系统配置
├── hardware/          # 硬件驱动
│   ├── max30102.c/h   # MAX30102 传感器驱动
│   ├── MPU6050.c/h    # MPU6050 六轴传感器驱动
│   ├── esp_simple.c/h # ESP8266 WiFi 透传驱动
│   ├── OLED.c/h       # OLED 显示驱动
│   ├── protocol.c/h   # 自定义二进制协议
│   ├── alth.c/h       # 心率/血氧算法
│   └── myi2c.c/h      # 软件 I2C 驱动
├── user/              # 用户应用代码
│   ├── main.c         # 基础版主函数
│   ├── main_mpu.c     # MPU6050 版主函数
│   ├── main_wifi.c    # WiFi 版主函数（推荐）
│   └── app_rtos.c     # FreeRTOS 任务注册
├── start/             # 启动文件和系统初始化
└── project.uvprojx    # Keil MDK 工程文件
```

## 🚀 快速开始

### 开发环境

- **编译器**：Keil MDK 5.x
- **调试器**：ST-Link V2
- **硬件平台**：STM32F103C8T6 最小系统板

### 编译步骤

1. 使用 Keil MDK 打开 `project.uvprojx`
2. 在 `User` 组中选择要编译的主函数文件：
   - `main_wifi.c`：WiFi 版（推荐）
   - `main_mpu.c`：MPU6050 版
   - `main.c`：基础版
3. 配置 `User` 组和 `Hardware` 组的编译选项
4. 编译并下载到开发板

### WiFi 配置

在 `user/main_wifi.c` 中修改 WiFi 配置：

```c
#define WIFI_SSID  "YOUR_WIFI_SSID"
#define WIFI_PASS  "YOUR_WIFI_PASSWORD"
#define PC_IP      "YOUR_PC_IP_ADDRESS"
```

## 📡 通信接口

### 串口配置

| 串口 | 波特率 | 数据位 | 停止位 | 校验位 | 用途 |
|------|--------|--------|--------|--------|------|
| USART1 | 115200 | 8 | 1 | None | 调试输出 + 协议帧发送 |
| USART2 | 115200 | 8 | 1 | None | ESP8266 WiFi 通信 |

### I2C 总线配置

| 总线 | SCL | SDA | 设备 |
|------|-----|-----|------|
| I2C1 (软件) | PA6 | PA7 | MAX30102, OLED |
| I2C2 (软件) | PB10 | PB11 | MPU6050 |

## 📊 性能指标

| 指标 | 数值 |
|------|------|
| 系统时钟 | 36MHz (HSI + PLL) |
| 心率采样率 | 25Hz |
| 心率计算周期 | 4s |
| MPU6050 采样率 | 100Hz |
| 运动检测更新率 | 10Hz |
| 协议帧开销 | 6 字节（含校验） |
| 堆空间 | 12KB |

## 📝 代码规范

- 采用 STM32 标准库风格
- 使用 FreeRTOS 标准 API
- 变量命名采用下划线分隔法 (`sensor_data`)
- 函数命名采用驼峰命名法 (`max30102_Init`)

## 📄 许可证

MIT License

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！
