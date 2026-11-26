<div align="center">
  <img src="icons/io.github.lp86ndlb.LuneInput.Settings.svg" width="72" height="72" alt="Lune 应用图标">
  <h1>Lune Input</h1>
  <p><strong>为 Ubuntu 桌面打磨的中文输入方案</strong></p>
  <p>基于 Fcitx5 与 Rime · 致敬朙月拼音</p>
  <p>
    <a href="#快速开始">快速开始</a> ·
    <a href="#按键和词库">按键与词库</a> ·
    <a href="#候选框动画">候选框动画</a> ·
    <a href="#调整配置">配置</a> ·
    <a href="#来源与致谢">致谢</a>
  </p>
  <p>
    <img src="https://img.shields.io/badge/Ubuntu-24.04-E95420?logo=ubuntu&logoColor=white" alt="Ubuntu 24.04">
    <img src="https://img.shields.io/badge/Fcitx5-5.1.7-526A83" alt="Fcitx5 5.1.7">
    <img src="https://img.shields.io/badge/Rime-朙月拼音-7D6BA3" alt="Rime 朙月拼音">
  </p>
  <img src="docs/candidates-hidpi.png" width="822" alt="Lune 输入 shurufa 时的横排九候选">
  <p><sub>Catppuccin Mocha 圆角主题 · 拼音按音节分隔 · 横排九候选</sub></p>
</div>

Lune 将朙月拼音、实用词库与桌面外观整合为一套输入方案，适合在 Ubuntu 上日常中文输入与中英混输。

- **输入体验**　横排九候选，左右 Shift 切换中英文，支持拼音纠错与前后鼻音配置。
- **词库**　支持英文候选、中英混输和长词补全，内置可手动更新的 IT 专业词表。
- **视觉设计**　Catppuccin Mocha 圆角主题、竖线编辑光标，以及适配 X11 的候选框右边缘过渡。

## 快速开始

适配基线为 **Ubuntu 24.04、GNOME、amd64**。以下命令在下载后的仓库根目录运行。只想体验主题、词库和九候选，完成本节即可。

```bash
sudo apt install fcitx5 fcitx5-rime fcitx5-chinese-addons fcitx5-config-qt \
  fcitx5-frontend-gtk3 fcitx5-frontend-gtk4 fcitx5-frontend-qt5 \
  rime-data-luna-pinyin rime-data-stroke librime-bin fonts-noto-cjk fonts-hanazono im-config \
  python3-gi gir1.2-gtk-3.0 python3-dbus python3-yaml
./scripts/install
im-config -n fcitx5
```

注销并重新登录。首次使用时给 Rime 一点时间编译词库。在文本框输入 `shurufa`，应该能看到「输入法」。已经使用 Fcitx5 的用户可以先从托盘菜单重启 Fcitx5，再在「中州韵」菜单中选择「重新部署」。

安装脚本以当前用户身份运行，会写入 Fcitx5 分组、快捷键、外观和 Rime 方案。已有文件内容不同时，旧版本会先保存到 `~/.local/state/lune-input/backups/` 下当次安装的目录，按原绝对路径分层存放。需要恢复某项设置时，将对应文件复制回原位置，再重启 Fcitx5。设置了 XDG 目录的用户以脚本输出的备份路径为准。

## 按键和词库

| 想做什么 | 操作或示例 |
| --- | --- |
| 切换中英文 | 轻按左 Shift 或右 Shift。两侧行为一致，切换时关闭光标旁的状态提示。 |
| 原样输入英文 | 启用下文的输入集成后，在中文模式输入 `hello`，轻按任一 Shift，`hello` 上屏并切到英文。 |
| 纠正颠倒的字母 | 输入 `hsang` 或 `shnag`，第二格提示 `shang〔纠错〕`。按 2 或点击提示，候选变为「上、商、伤……」，再选字。 |
| 纠正多打的字母 | 输入 `qiing`，第二格提示 `qing〔纠错〕`。连打 `qiingwen` 也能按 2 改回 `qingwen`，继续选「请问」。 |
| 选择候选词 | 每页最多九个，数字 1–9 直接选词。拼音光标在末尾时按 → 选下一个，空格确认。 |
| 查看拼音切分 | 输入 `shurufa` 显示为 `shu'ru'fa`。单引号标记拼音引擎识别的音节边界。 |
| 改一个拼错的字母 | 按 ← 回到拼音中间，用竖线定位。此时 → 先向右移动光标。 |
| 区分前后鼻音 | 仅对 en/eng、in/ing 开启模糊音。an/ang 保持区分。 |
| 中英混输 | 简拼 `key` 优先候选「可以」。输入 `wifimima` 可候选「Wi-Fi密码」，`macosbanben` 可候选「macOS版本」。 |
| 补全长词 | 输入 `caozuox` 可候选「操作系统」，`rengongzh` 可候选「人工智能」。 |
| 输入等号 | 没有正在编辑的拼音时，`=` 直接上屏。有候选时，`=` 和 `-` 可翻页。 |

仓库附带八股文词频、雾凇拼音的英文与中英混输词表，以及从雾凇基础词表生成的长词前缀。IT 专业词来自清华大学开放中文词库，日常输入可离线使用。需要更新 IT 词表时，在 Fcitx5 正在运行的桌面会话中执行以下命令。

```bash
sudo apt install icu-devtools
~/.local/bin/update-rime-it-dict
```

## 候选框动画

候选文字始终按自然字宽绘制。小幅刷新直接显示，明显的宽度变化才让右边框过渡。连续退格时，边框跟随内容收回。前面的词变长时，后面的词可以让位，所有候选保持单行全文。

这个补丁针对 **X11、Fcitx5 5.1.7、Ubuntu 软件包 5.1.7-1build3 的 amd64 版 Classic UI**。构建脚本校验发行版插件和 Ubuntu 源码包。启动时加载器会再次核对系统插件，在 Wayland 或系统插件版本变化后自动使用发行版界面。

<details>
<summary>在匹配的 X11 环境编译并启用右边框动画</summary>

```bash
sudo apt install build-essential pkg-config patch xz-utils binutils \
  fcitx5-modules-dev libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev \
  libglib2.0-dev libcairo2-dev libpango1.0-dev libgdk-pixbuf-2.0-dev libfmt-dev \
  libxcb-util-dev libxcb-shape0-dev libxcb-ewmh-dev libxcb-icccm4-dev \
  libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render0-dev \
  libxcb-xfixes0-dev libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-x11-dev
./scripts/build-renderer
./scripts/install --with-renderer
```

构建时会下载 Fcitx5 5.1.7 源码。如已有原始压缩包，可运行 `./scripts/build-renderer --archive /path/to/fcitx5_5.1.7.orig.tar.xz`。安装完成后重启 Fcitx5。将下表中的动画时长设为 `0` 可以关闭过渡。要完全切回系统渲染组件，将当前用户数据目录里的 `fcitx5/addon/classicui.conf` 移出该目录，再重启 Fcitx5。

</details>

## 调整配置

在应用列表搜索 **Lune**，点击淡紫色月牙图标打开设置。修改选项后点击「应用」。窗口包含三个分页。

| 分页 | 可调整的选项 |
| --- | --- |
| 输入 | Shift 切换时保留原文、第二候选纠错提示、en/eng 与 in/ing 模糊音、音节分隔符 |
| 候选框 | 每页 1–9 项、候选字体及字重、右边框动画时长 |
| 词库 | 英文候选及权重、中英混输、专业词与长词补全、检查 IT 词库更新 |

**输入集成**为托盘添加 **右键 → Lune Input → 配置…**，在中英文模式下都能打开，也提供 Shift 原文上屏和第二候选纠错。适配 Ubuntu 24.04 amd64 的 `fcitx5-rime 5.1.4-2build2`。在仓库根目录执行以下命令，然后重启 Fcitx5。

```bash
sudo apt install build-essential pkg-config patch xz-utils fcitx5-modules-dev \
  libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev libglib2.0-dev \
  librime-dev libgoogle-glog-dev libboost-dev libfmt-dev
./scripts/build-integration
./scripts/install --with-integration
```

已有 Lune 配置的用户，把最后一条安装命令换成 `./scripts/install --upgrade-integration`，即可保留现有分组、快捷键、外观和词库。

「第二候选提示拼音纠错」同时控制字母颠倒和误连按。颠倒字母支持单个音节。误连按支持连续拼音，一次检查一处重复字母，优先删去最少的字母，得到唯一可完整切分的拼写时才提示。`jinnian`（今年）和 `zhongguo`（中国）中相邻的相同字母各属两个音节，会按原拼写出词。第一格照常选用，每页只有一项时会隐藏纠错提示。

Rime 选项保存在当前用户的 `fcitx5/rime/lune_preferences.yaml`，点击「应用」会自动请求重新部署。Shift 和外观设置通过正在运行的 Fcitx5 保存。边框时长需要上文的 X11 动画组件。两个构建完成后也可以运行 `./scripts/install --with-renderer --with-integration` 一并安装。系统 Rime 前端升级后，加载器会先使用发行版插件。

更多主题和候选方向设置位于 `fcitx5-configtool` 的「附加组件 → 经典用户界面」。默认候选字体为 Noto Sans CJK SC Medium 11，扩展汉字由花园明朝字体补充显示。

| 设置 | 仓库文件 | 默认值 |
| --- | --- | --- |
| 字体、横排、边框动画时长 | [classicui.conf](fcitx5/conf/classicui.conf) | 横排，168 毫秒 |
| 模糊音、候选数、英文候选权重 | [朙月拼音补丁](rime/luna_pinyin_simp.custom.yaml) | 九候选，英文参与混排 |
| 中英文切换和提示 | [Fcitx5 全局配置](fcitx5/config) | 左右 Shift 均可切换 |
| 圆角、颜色和选中态 | [主题目录](themes/catppuccin-mocha-mauve/) | Mocha Mauve |
| X11 候选框渲染 | [补丁和加载器源码](renderer/) | 只对匹配版本启用 |
| Shift 原文与第二格纠错 | [输入集成源码](integration/) | 安装输入集成后默认开启 |

仓库里的文件是安装源。修改仓库后，按原来的安装方式再运行一次脚本。直接调整本机设置时，Fcitx5 配置默认在 `~/.config/fcitx5/`，Rime 方案在 `~/.local/share/fcitx5/rime/`。修改 Rime 的 YAML 后需重新部署。

## 来源与致谢

| 上游项目 | Lune 使用的部分 |
| --- | --- |
| [Fcitx5](https://github.com/fcitx/fcitx5) 与 [fcitx5-rime](https://github.com/fcitx/fcitx5-rime) | 桌面输入框架、Rime 前端及 Classic UI。仓库中的 X11 补丁基于 5.1.7。 |
| [Rime](https://github.com/rime/librime)、[朙月拼音](https://github.com/rime/rime-luna-pinyin) 与 [八股文](https://github.com/rime/rime-essay) | 输入引擎、简体拼音方案及 `essay.txt` 词频数据 |
| [雾凇拼音](https://github.com/iDvel/rime-ice) | 英文词库、融合拼音方案、中英词表和长词前缀的数据来源。英文词库文件头保留了原始来源。 |
| [THUOCL](https://github.com/thunlp/THUOCL) | 清华大学开放中文词库中的 IT 词表 |
| [Catppuccin for Fcitx5](https://github.com/catppuccin/fcitx5) | Mocha Mauve 配色与主题资源，Lune 调整了候选框和高亮项的圆角 |

各上游的许可和署名收录于 [licenses/](licenses/)。Rime 的个人学习词库、同步目录和输入历史保存在本机。
