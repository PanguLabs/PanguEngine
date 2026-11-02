# Claude Code 项目说明

@AGENTS.md

请遵循仓库根目录的 `AGENTS.md`；它是本项目通用 Agent 开发规范的唯一权威来源。不要在此文件重复维护 C++、构建系统或架构规则；如需修改通用规则，请直接更新 `AGENTS.md`。

共享 Skills 统一维护在 `.agents/skills/`。如果缺少 `.claude/skills`，请在仓库根目录执行：

```bash
python Scripts/setup-agent-links.py
```

## `.claude` 目录存放约定

需要放入 `.claude` 目录的文件（配置、规则、skill、hook 等）**优先使用本项目的 `.claude/`**，不要放在用户空间的 `~/.claude/`：项目级配置随仓库一起版本管理，团队成员与后续的 Claude 实例都能直接获得，不依赖某个人的本机环境。由 Claude Code 自身管理、路径固定的文件（例如会话的 memory 目录）不受此约定约束。
