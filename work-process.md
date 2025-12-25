
# Git Rebase 极简协作手册 (C++版)

本手册专为 Git 新手设计，采用 **Rebase 工作流**，确保我们的提交历史像一条直线一样干净。

## ⚠️ 绝对红线 (死命令)
1.  **永远不要在 `main` 分支上执行 rebase！**
2.  **永远不要 rebase 别人的分支！**
3.  只在你自己的 **私有功能分支 (feature/xxx)** 上 rebase `main`。

---

## 第一阶段：项目初始化 (只需做一次)

如果你是第一次加入项目，请按以下步骤操作。

### 1. 获取代码 (Clone)
找到 GitHub 仓库页面绿色的 "<> Code" 按钮，复制 URL。

打开终端 (Terminal/CMD) 或 Git Bash：
```bash
# 下载代码到本地
git clone https://github.com/你的用户名/项目名.git

# 进入项目文件夹 (非常重要！很多新手忘了这一步)
cd 项目名
```

### 2. 检查 C++ 环境配置
确保项目根目录有 `.gitignore` 文件。如果没有，请立即创建一个，否则编译产生的 `.exe`, `.o`, `build/` 文件夹会把仓库弄得很脏。

---

## 第二阶段：日常开发循环 (每天都在做)

### 1. 也是第一步：确保本地 main 是最新的
在开始任何新工作前，先更新主干。
```bash
git checkout main
git pull origin main
```

### 2. 创建新分支 (起跑)
基于最新的 main 创建你的任务分支。
```bash
# 格式：git checkout -b <分支名>
git checkout -b feature/login-ui
```

### 3. 写代码 & 提交 (存档)
编写你的 C++ 代码。当你完成一个小阶段（比如写完一个函数，或者能通过编译了）：
```bash
# 把所有修改加入暂存区
git add .

# 提交并附带说明
git commit -m "实现登录窗口布局"
```
*此时，你可以在这个分支上反复提交多次 (commit 1, commit 2, ...)*

---

## 第三阶段：同步最新代码 (Rebase 核心操作)

当你开发了一半，或者准备发布代码时，队友可能已经更新了 `main`。**这时候你需要把你的代码“搬运”到最新的 `main` 后面。**

### 1. 更新本地 main
```bash
# 先切回主分支
git checkout main

# 拉取队友推送的最新代码
git pull origin main
```

### 2. 执行 Rebase (变基)
```bash
# 切回你的开发分支
git checkout feature/login-ui

# 核心指令：把我当前的修改，接在 main 的最后面
git rebase main
```

### 🚨 情况 A：一切顺利
如果没有冲突，Git 会提示 `Successfully rebased`。恭喜你，跳到第四阶段！

### 🚨 情况 B：发生冲突 (CONFLICT)
这是新手最怕的环节，别慌！Rebase 是一次搬运一个 commit，如果有冲突，Git 会停下来让你修。

1.  **打开代码编辑器** (VS Code / VS)。
2.  找到标红的文件，你会看到 `<<<<<<< HEAD` (你的代码) 和 `>>>>>>> main` (新进来的代码)。
3.  **手动修改代码**：决定保留哪部分，或者把两部分融合，删除那些尖括号符号。
4.  **回到终端**，告诉 Git 你修好了：
    ```bash
    git add .
    
    # ⚠️ 注意：这里不要运行 git commit，而是运行：
    git rebase --continue
    ```
5.  如果还有冲突，重复上述步骤，直到提示 `Successfully rebased`。

---

## 第四阶段：推送到 GitHub

### 1. 首次推送 / 普通推送
如果你是新建的分支，且**从未使用过** rebase，直接推：
```bash
git push -u origin feature/login-ui
```

### 2. 强制推送 (Rebase 后的必须操作)
如果你之前已经 push 过这个分支，然后又在本地执行了 `rebase`，你的本地历史和远程历史就不一样了。此时普通的 `push` 会报错。

**你必须强制推送 (覆盖远程分支)：**
```bash
# 使用 --force-with-lease 比单纯的 --force 更安全
# 它的意思是："如果没人动过我的远程分支，就覆盖它"
git push --force-with-lease
```

---

## 第五阶段：发起合并 (PR)

1.  打开 GitHub 仓库页面。
2.  GitHub 通常会弹出一个黄色横条提示 "feature/login-ui had recent pushes"，点击 **"Compare & pull request"**。
3.  **Title**: 简述你做了什么。
4.  **Reviewers**: 在右侧选择你的队友。
5.  点击 **Create pull request**。

队友 Review 通过后，点击 **"Squash and merge"** 或 **"Rebase and merge"** (取决于仓库设置) 将代码并入 main。

---

## ⑥ 完结撒花：开始下一个任务

代码合并进 main 后，你的本地分支使命结束：

```bash
# 1. 切回主干
git checkout main

# 2. 拉取包含你自己刚刚贡献的最新代码
git pull origin main

# 3. 删除旧的开发分支
git branch -d feature/login-ui
```

回到第二阶段，开始新的循环！
