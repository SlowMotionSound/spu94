# Project Structure Reference

This project follows the standard GSD + Production AI folder layout.

## Core Folders and Their Purpose

- **services/**          → Reusable core logic: utilities, API clients, data processing, caching, retrieval, RAG pipelines
- **agents/**            → Major features, workers, modules, or AI agents (use components/ or modules/ if the project is not agentic)
- **prompts/**           → All prompts, templates, instruction files, and LLM configs (or general rules/instructions)
- **security/**          → Authentication, input/output guards, validation, sanitization, error handling
- **evaluation/**        → Tests, golden datasets, quality checks, offline/online evaluation scripts
- **observability/**     → Logging, tracing, metrics, monitoring, debugging helpers
- **config/**            → Configuration files, environment settings, constants
- **utils/**             → Small shared helper functions (only if services/ would feel too heavy)
- **tests/**             → Unit and integration tests (can overlap with evaluation/)
- **docs/**              → Documentation, architecture notes, READMEs
- **.claude/**           → GSD rules, CLAUDE.md, persistent instructions, project conventions
- **.planning/**         → GSD memory files (PROJECT.md, REQUIREMENTS.md, ROADMAP.md, STATE.md, phase plans)

## Root Files (common)
- main.py or app.py      → Main entry point
- README.md              → Project overview and setup instructions
- .env.example           → Example environment variables
- PLAN.md                → Current GSD plan (may also live in .planning/)

## Key Rules for GSD and File Generation
- Always place new code in the appropriate folder based on its responsibility.
- Keep concerns separated: one folder = one clear purpose.
- When creating or editing files, follow this structure unless explicitly told otherwise.
- GSD management folders (.planning/ and .claude/) sit alongside the code layers and should not be mixed with source code.
- If a folder does not perfectly fit the project type, you may rename or merge (e.g., agents/ → components/), but keep the overall separation of concerns.

This reference helps keep every project clean, maintainable, and easy for AI coding tools to understand.
