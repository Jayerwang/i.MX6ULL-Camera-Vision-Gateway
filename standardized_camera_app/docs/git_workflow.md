# Git Workflow for a New Project

本文档总结一个新项目从 0 新建 Git 仓库、连接 GitHub、日常管理版本的推荐顺序。

## 1. 整理项目目录

先把真正属于项目的内容放进仓库目录。

示例：

```text
i.MX6ULL-Camera-Vision-Gateway/
  README.md
  Makefile
  include/
  src/
  docs/
  scripts/
  tests/
```

不要把无关实验目录、编译产物、图片输出、临时文件放进 Git 仓库。

## 2. 写 .gitignore

先写 `.gitignore`，再执行 `git add`，避免误提交无关文件。

常见忽略规则：

```gitignore
build/
output/
*.o
*.elf
*.bin
*.yuv
*.mjpg
*.jpg
*.log
*.tmp
.vscode/
```

如果只想上传某一个子项目，可以使用白名单方式：

```gitignore
*
!standardized_camera_app/
!standardized_camera_app/**
```

## 3. 初始化 Git 仓库

```bash
git init
```

查看当前状态：

```bash
git status
```

## 4. 添加文件

添加全部文件：

```bash
git add .
```

只添加某个项目目录：

```bash
git add standardized_camera_app
```

## 5. 第一次提交

```bash
git commit -m "init camera vision gateway project"
```

## 6. 在 GitHub 新建远程仓库

例如仓库名：

```text
i.MX6ULL-Camera-Vision-Gateway
```

GitHub 会提供远程地址，例如：

```bash
https://github.com/用户名/i.MX6ULL-Camera-Vision-Gateway.git
```

## 7. 绑定远程仓库

```bash
git remote add origin https://github.com/用户名/i.MX6ULL-Camera-Vision-Gateway.git
```

检查远程仓库：

```bash
git remote -v
```

## 8. 推送到 GitHub

如果本地分支不是 `main`，可以先改名：

```bash
git branch -M main
```

第一次推送：

```bash
git push -u origin main
```

以后可以直接：

```bash
git push
```

## 9. 日常开发流程

每次修改代码后，推荐按这个顺序操作：

```bash
git status
git diff
git add 修改的文件
git commit -m "feat: add http mjpeg streaming"
git push
```

常见 commit message 前缀：

```text
feat: 新功能
fix: 修 bug
docs: 文档
test: 测试
chore: 项目整理、构建配置
refactor: 重构
```

示例：

```bash
git commit -m "feat: add lcd framebuffer preview"
git commit -m "fix: handle mjpeg capture timeout"
git commit -m "docs: record motion detection validation"
git commit -m "chore: publish only camera gateway app"
```

## 10. 拉取远程更新

如果本地已经有仓库：

```bash
git pull
```

如果本地没有仓库：

```bash
git clone https://github.com/用户名/仓库名.git
```

## 11. 打阶段版本 tag

当项目到一个稳定阶段，可以打 tag。

示例：

```bash
git tag v0.9-motion-gateway
git push origin v0.9-motion-gateway
```

以后可以切回该版本：

```bash
git checkout v0.9-motion-gateway
```

tag 适合标记阶段性成果，例如：

```text
v0.1-basic-capture
v0.5-http-mjpeg
v0.8-lcd-preview
v0.9-motion-gateway
v1.0-camera-vision-gateway
```

## 12. 清理误提交文件

如果某个文件已经被 Git 跟踪，但你不想继续上传到 GitHub，可以只从 Git 索引移除，不删除本地文件：

```bash
git rm --cached 文件名
git commit -m "chore: remove unrelated files from repo"
git push
```

如果是目录：

```bash
git rm --cached -r 目录名
git commit -m "chore: remove unrelated directory from repo"
git push
```

## 13. 推荐完整顺序

```text
整理目录
-> 写 .gitignore
-> git init
-> git status
-> git add
-> git commit
-> GitHub 建仓库
-> git remote add origin
-> git push -u origin main
-> 日常 git status / diff / add / commit / push
-> 阶段稳定后打 tag
```

## 14. 本项目建议

本项目 GitHub 仓库应只发布：

```text
standardized_camera_app/
```

项目正式名称：

```text
i.MX6ULL Camera Vision Gateway
```

不属于本项目的实验目录、历史代码、临时文档、编译输出，不应上传到 GitHub。
