# Mysterious Electronic Photo Album 📸 (奇妙电子相册)

基于 **Qt6 (C++)** 开发的高性能桌面端多功能电子相册应用程序。支持相册项目管理、树形目录导航、图片高清等比缩放浏览、双向链表前后翻页、全屏幻灯片平滑淡入淡出轮播、背景音乐播放以及多线程异步导入与打开等功能。

---

## 🌟 核心功能与技术亮点

- **📁 相册项目管理**：支持通过向导创建新相册项目与打开已有相册，支持项目路径去重与本地项目删除确认。
- **🌲 树形目录与双向链表**：自定义树节点结合**双向链表**设计，实现跨多层级文件夹的照片线性前后翻页切换。
- **🖼️ 高清等比例平滑缩放**：采用事件过滤器与 `QPainter` 抗锯齿/双线性插值渲染，彻底避免缩放失真与窗口布局挤压。
- **🎞️ 全屏幻灯片与平滑淡入淡出**：支持自动轮播与手动翻页，基于透明度混合插值算法实现高帧率无缝过渡。
- **🎵 背景音乐播放**：集成 Qt6 `QMediaPlayer` 与 `QAudioOutput`，支持背景音乐无缝循环播放与轮播联动。
- **⚡ 多线程异步 IO**：项目扫描、图片拷贝与结构初始化由 `QThread` 异步执行，配合进度对话框，保证 UI 界面丝滑流畅。
- **🎨 现代暗色 QSS 主题**：全套定制的高颜值深色主题与自定义三态图标控件。

---

## 🏛️ 项目核心类功能详解 (Class Documentation)

本项目模块化清晰，各核心类的职责分工如下：

### 1. 窗口与框架管理
| 类名 | 文件 | 功能描述 |
| :--- | :--- | :--- |
| **`MainWindow`** | `mainwindow.h/cpp/ui` | **主窗口类**。负责程序主框架布局、顶部菜单栏（文件/设置/背景音乐）创建与事件分发，管理左侧项目树与右侧图片展示区的信号路由。 |
| **`Wizard`** | `wizard.h/cpp/ui` | **相册创建向导类**。继承自 `QWizard`，以向导步骤形式引导用户创建相册，完成后向主界面发射相册配置参数信号。 |
| **`ProSetPage`** | `prosetpage.h/cpp/ui` | **向导设置页**。向导第一页，负责采集相册名称与保存路径，实时校验路径存在性与防止重名。 |
| **`ConfirmPage`** | `confirmpage.h/cpp/ui` | **向导完成确认页**。向导第二页，展示项目创建就绪信息。 |

### 2. 相册目录树模块
| 类名 | 文件 | 功能描述 |
| :--- | :--- | :--- |
| **`ProTree`** | `protree.h/cpp/ui` | **左侧相册树容器窗口**。封装 `ProTreeWidget` 并设定固定宽度与布局，对外提供统一的项目添加接口。 |
| **`ProTreeWidget`** | `protreewidget.h/cpp` | **核心相册树控件**。继承自 `QTreeWidget`，实现右键菜单（导入、设为活动项目、关闭相册、幻灯片播放）、双击图片联动、启动多线程异步操作与管理 `QMediaPlayer` 背景音乐。 |
| **`ProTreeItem`** | `protreeitem.h/cpp` | **自定义相册树节点项**。继承自 `QTreeWidgetItem`，封装项目根节点、文件夹与图片属性，内置前驱 `_pre_item` 与后继 `_next_item` 指针构建全局双向链表。 |

### 3. 多线程任务模块
| 类名 | 文件 | 功能描述 |
| :--- | :--- | :--- |
| **`ProTreeThread`** | `protreethread.h/cpp` | **相册创建/图片导入后台线程**。继承自 `QThread`，在后台递归遍历源文件夹、安全拷贝图片文件至相册目录、构建双向链表节点，并实时通知主线程刷新进度条。 |
| **`OpenTreeThread`** | `opentreethread.h/cpp` | **打开已有相册后台线程**。继承自 `QThread`，后台递归扫描现有相册目录结构并还原相册目录树与照片双向链表。 |

### 4. 图片主展示模块
| 类名 | 文件 | 功能描述 |
| :--- | :--- | :--- |
| **`PicShow`** | `picshow.h/cpp/ui` | **主界面右侧图片展示窗口**。通过事件过滤器拦截 Paint 事件进行高清原图等比例居中绘制；监听鼠标 Enter/Leave 事件触发翻页按钮的平滑透明度淡入淡出。 |
| **`RemoveProDialog`** | `removeprodialog.h/cpp/ui` | **删除/关闭相册确认弹窗**。提供“同时删除本地磁盘文件”复选框选项，供用户确认删除行为。 |

### 5. 幻灯片轮播模块
| 类名 | 文件 | 功能描述 |
| :--- | :--- | :--- |
| **`SlideShowDlg`** | `slideshowdlg.h/cpp/ui` | **全屏幻灯片播放对话框**。无边框全屏设计，组合轮播动画区、播放/暂停控制栏、关闭按钮与底部预览缩略图栏，并与背景音乐自动联动。 |
| **`PicAnimationWid`** | `picanimationwid.h/cpp` | **轮播动画核心渲染控件**。通过定时器（60fps）与透明度因子（`_factor`）插值，实现上一张图片淡出与下一张图片淡入的高清平滑过渡效果。 |
| **`ProListWid`** | `prolistwid.h/cpp` | **底部缩略图列表控件**。继承自 `QListWidget`，以图标模式展示相册所有照片缩略图，支持点击跳转大图及与轮播进度同步。 |
| **`PreListItem`** | `prelistitem.h/cpp` | **缩略图项单元**。继承自 `QListWidgetItem`，封装缩略图图标、图片路径与全局序列索引。 |

### 6. 自定义 UI 交互组件
| 类名 | 文件 | 功能描述 |
| :--- | :--- | :--- |
| **`PicBtn`** | `picbtn.h/cpp` | **三态图片按钮**。支持设置普通态（Normal）、悬浮态（Hover）、按下态（Pressed）三套图标并自适应切换。 |
| **`PicStateBtn`** | `picstatebtn.h/cpp` | **复合双状态切换按钮**。支持播放态与暂停态两组状态（共 6 态图标）的自响应与程序控制翻转。 |

---

## 🛠️ 构建与运行指南

### 环境要求
- **C++ 标准**：C++17 或更高
- **Qt 版本**：Qt 6.5+ (推荐 Qt 6.5 ~ 6.8)
- **必需 Qt 模块**：`Qt::Core`, `Qt::Widgets`, `Qt::Multimedia`
- **构建工具**：CMake (3.19+) 或 Qt Creator

### 使用 CMake 命令行构建运行

Windows 下使用 **Developer PowerShell for VS 2022（x64）**，确保已安装 MSVC C++ 工具与 Windows SDK。以下命令从项目根目录执行，Qt 和 Ninja 路径按本机安装位置调整：

```powershell
cmake -S . -B build/windows-release -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 `
    -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe
cmake --build build/windows-release --parallel

# 部署 Qt DLL、平台插件和多媒体插件，随后可直接双击 EXE
& C:/Qt/6.8.3/msvc2022_64/bin/windeployqt.exe --release build/windows-release/Album.exe
& ./build/windows-release/Album.exe
```

使用 Qt Creator 时，选择 `Desktop Qt 6.8.3 MSVC2022 64bit` Kit，重新运行 CMake 后构建、运行。MSVC 版 Qt 必须搭配 MSVC 编译器；MinGW/GCC 不能与其混用。

### 启动失败排查

- **编译成功但启动即退出，错误码 `0xC0000374`**：已确认旧构建目录存在头文件依赖丢失。MSVC 的中文 `/showIncludes` 前缀编码与 Ninja 配置不匹配，导致修改头文件后仍链接旧对象文件，出现对象大小不一致和堆损坏。CMake 配置现已通过探测编译器原始输出修复此前缀。已有构建目录需重新运行 CMake，并执行一次 `cmake --build <构建目录> --clean-first`。
- **直接打开 EXE 提示缺少 DLL，或返回 `0xC0000135`**：使用对应 Qt Kit 的 `windeployqt.exe` 部署运行库。Debug 构建使用 `--debug`，Release 构建使用 `--release`。
- **`CMAKE_MAKE_PROGRAM-NOTFOUND`**：配置时明确指定 Ninja 路径，并在 VS 开发者终端中运行。使用独立的构建目录，避免复用其他编译器留下的缓存。

---

## 📄 开源许可证
本项目遵循 [MIT License](LICENSE) 开源协议。
