# MyAndroidDrillsApp 🛠️

> Android Drills 规则引擎示例项目 - 演示如何在 Android 应用中集成 Drills C++ 规则引擎

[![Minimum SDK Version](https://img.shields.io/badge/Minimum%20SDK-API%2024%20(7.0)-brightgreen.svg)](https://developer.android.com/studio/releases/platforms)
[![Language](https://img.shields.io/badge/Kotlin-100%25-blue.svg)](https://kotlinlang.org/)
[![Platform](https://img.shields.io/badge/Android-100%25-green.svg)](https://developer.android.com/)

## 📋 目录

- [项目简介](#项目简介)
- [系统要求](#系统要求)
- [快速开始](#快速开始)
- [手动设置](#手动设置)
- [构建和部署](#构建和部署)
- [故障排除](#故障排除)
- [项目结构](#项目结构)
- [兼容性](#兼容性)

## 📖 项目简介

这是一个完整的 Android 示例项目，演示了如何通过 JNI (Java Native Interface) 在 Android 应用中调用 C++ Drills 规则引擎。该项目展示了：

- ✅ Kotlin 与 C++ 的 JNI 集成
- ✅ Drills 规则引擎的核心功能调用
- ✅ Android NDK 原生库编译配置
- ✅ 跨平台 ABI 支持

启动应用后，您可以通过界面按钮完成规则引擎的完整生命周期操作。

## 💻 系统要求

### 开发环境必需组件
| 组件 | 版本要求 | 下载 |
|------|----------|------|
| Android Studio | Arctic Fox (2020.3.1+) 或更高版本 | [官网下载](https://developer.android.com/studio) |
| Android SDK | API 24+ (Android 7.0+) | Android Studio 中安装 |
| Android NDK | r21e+ | Android Studio → SDK Manager |
| JDK/JRE | 11+ | Android Studio 捆绑版本 |

### 硬件要求
- **最低配置**: 4GB RAM, 2GB 可用磁盘空间
- **推荐配置**: 8GB+ RAM, 支持 HAXM 或 Hyper-V 的 CPU

## 🚀 快速开始

### 方法一：一键导入项目 (推荐)

1. **克隆项目**
   ```bash
   git clone https://github.com/your-repo/drills.git
   cd drills/MyAndroidDrillsApp
   ```

2. **打开 Android Studio**
   ```bash
   # Windows
   start android-studio
   ```

3. **导入项目**
   - 选择 `File → Open`
   - 导航到 `MyAndroidDrillsApp` 目录
   - 点击 `OK` 并等待 Gradle 同步完成

4. **放置本地库文件**
   你需要将预编译的 `libdrills_capi.so` 文件放置到相应目录中。

5. **运行项目**
   - 连接 Android 设备或启动模拟器
   - 点击绿色的 **Run** ▶️ 按钮

### 🚨 重要提示
如果遇到 Gradle 同步错误，请先执行以下命令：
```bash
# 清理 Gradle 缓存
./gradlew clean
./gradlew build
```

## ⚙️ 手动设置

如果你需要从头创建一个新项目，请按以下步骤操作：

### 1. 创建新的 Android Studio 项目

```kotlin
Project Configuration {
    Name: MyAndroidDrillsApp
    Package: com.example.drillsandroidapp
    Language: Kotlin
    Minimum SDK: API 24: Android 7.0 (Nougat)
    Template: Empty Activity
}
```

### 2. 配置项目文件

将以下项目文件复制到对应的目录结构中：

#### 核心配置文件
```text
MyAndroidDrillsApp/
├── settings.gradle                       # 项目设置
├── build.gradle                         # 根级构建配置
└── app/
    ├── build.gradle                     # 应用构建配置
    ├── src/main/
    │   ├── AndroidManifest.xml          # 应用清单
    │   ├── java/
    │   │   └── com/example/drillsandroidapp/
    │   │       ├── MainActivity.kt     # 主 Activity
    │   │       └── DrillsEngine.kt     # Drills 引擎封装
    │   ├── cpp/
    │   │   ├── CMakeLists.txt          # CMake 构建脚本
    │   │   └── native-lib.cpp          # JNI 原生接口
    │   ├── res/
    │   │   ├── layout/activity_main.xml # 主界面布局
    │   │   └── values/strings.xml      # 字符串资源
    │   └── jniLibs/
    │       └── [ABI]/libdrills_capi.so  # Drills CAPI 库
```

### 3. 配置 NDK 库路径

根据你的目标 ABI 创建相应的本地库目录：

```bash
# 创建 ABI 目录结构
mkdir -p app/src/main/jniLibs

# 支持的 ABI 列表
mkdir -p app/src/main/jniLibs/{armeabi-v7a,arm64-v8a,x86,x86_64}
```

#### ABI 支持说明
| ABI | 架构 | 支持情况 |
|-----|------|----------|
| `armeabi-v7a` | 32位 ARM | ✅ 必需 |
| `arm64-v8a` | 64位 ARM | ✅ 必需 |
| `x86` | Intel x86 | ✅ 可选 (模拟器) |
| `x86_64` | Intel x64 | ✅ 可选 (模拟器) |

### 4. Gradle 配置验证

确保 `app/build.gradle` 中的 NDK 配置正确：

```gradle
android {
    defaultConfig {
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a' // 根据需要调整
        }
    }
}
```

## 🏗️ 构建和部署

### 构建项目
```bash
# 调试构建
./gradlew assembleDebug

# 发布构建
./gradlew assembleRelease

# 构建并安装到已连接设备
./gradlew installDebug
```

### 部署到设备
1. **连接设备**
   - 启用 USB 调试模式
   - 允许 USB 安装

2. **验证部署**
   ```bash
   # 查看连接的设备
   adb devices
   ```

3. **启动应用**
   - 在设备上点击应用图标启动
   - 或通过 Android Studio 的 **Run** 按钮

## 🔧 故障排除

### 常见问题及解决方案

#### ❌ Gradle 同步失败
```
Cause: unsupported class file major version 61
```
**解决方案**:
1. 更新 Android Gradle Plugin 到最新版本
2. 检查 JDK 版本是否为 17+
3. 清理 Gradle 缓存: `./gradlew cleanBuildCache`

#### ❌ NDK 构建失败
```
Cause: No toolchains found in the NDK toolchains folder
```
**解决方案**:
1. 通过 SDK Manager 下载最新 NDK
2. 确认 `local.properties` 中的 NDK 路径正确
3. 清理并重建: `./gradlew clean; ./gradlew build`

#### ❌ 运行时崩溃
```
java.lang.UnsatisfiedLinkError: dalvik.system.PathClassLoader[...]: couldn't find "libdrills_capi.so"
```
**解决方案**:
1. 确认 `libdrills_capi.so` 文件存在于正确的 ABI 目录
2. 检查 `build.gradle` 中的 ABI 过滤器配置
3. 清理项目并重建

#### ❌ 设备兼容性问题
**现代设备支持**:
- ✅ Android 7.0+ (API 24+)
- ⚠️ Android 6.0 以下版本不支持

#### 🚨 磁盘空间不足
临时解决方案:
```bash
# 清理 Android Studio 缓存 (Windows)
rmdir /s %USERPROFILE%\.gradle\caches
```

### 调试技巧

#### 查看详细构建日志
```bash
# 启用详细输出
./gradlew build --info
./gradlew assembleDebug --debug
```

#### NDK 日志分析
```bash
# 查看 NDK 构建详情
./gradlew build 2>&1 | findstr NDK
```

## 📁 项目结构

```
MyAndroidDrillsApp/
├── app/
│   ├── build.gradle                      # 应用级构建配置
│   ├── proguard-rules.pro                # 混淆规则
│   └── src/
│       ├── main/
│       │   ├── java/                     # Kotlin/Java 源码
│       │   ├── cpp/                      # C++ 原生代码
│       │   ├── res/                      # 资源文件
│       │   ├── AndroidManifest.xml       # 应用配置
│       │   └── jniLibs/                  # 原生库
│       └── androidTest/                  # 测试代码
├── build.gradle                          # 根构建配置
├── settings.gradle                       # 项目设置
├── gradle.properties                     # Gradle 属性
└── README.md                             # 项目文档
```

## 🔄 兼容性

### Android 版本支持
- **最低版本**: Android 7.0 (API 24)
- **目标版本**: Android 13+ (API 33)
- **编译版本**: Android 14 (API 34)

### 实验性功能
- ✅ Custom Build Variants
- ✅ Multi-ABI Native Libraries
- ✅ Instant Run Support

---

## 🤝 贡献指南

1. Fork 本项目
2. 创建特性分支: `git checkout -b feature/new-feature`
3. 提交更改: `git commit -am 'Add new feature'`
4. 推送到分支: `git push origin feature/new-feature`
5. 创建 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 查看 [LICENSE](../LICENSE) 文件了解详情

## 📞 技术支持

如遇到技术问题，请：
1. 检查本文档的故障排除部分
2. 查看项目 Issues 页面
3. 提交新的 Issue 描述问题

---

**最后更新时间**: 2024年9月3日
