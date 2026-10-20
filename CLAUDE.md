# Claude Code 项目说明

@AGENTS.md

请遵循仓库根目录的 `AGENTS.md`；它是本项目通用 Agent 开发规范的唯一权威来源。

共享 Skills 统一维护在 `.agents/skills/`。如果缺少 `.claude/skills`，请在仓库根目录执行：

```bash
python Scripts/setup-agent-links.py
```

不要在此文件重复维护 C++、构建系统或架构规则；如需修改通用规则，请直接更新 `AGENTS.md`。
