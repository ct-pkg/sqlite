# sqlite 包交付报告（Wave 2 / Phase 2）

> 后续变更：0.1.0 已把函数式的 `Db.h.ct` / `Stmt.h.ct` 收进类型宿主，句柄方法挂在包装类型
> `SqliteDB *` / `SqliteStmt *` 上（包装壳由 `Sqlite.shim.h` 定义，`close` /
> `finalize` 幂等）；本文档中涉及 `db_*` / `stmt_*`、C 层宿主与「重复关闭是 misuse」的
> 描述为历史记录，当前 API 见 [README.md](README.md) 与 [docs/tutorial.md](docs/tutorial.md)。
> 新旧 API 的 28 条逐条对照、覆盖证据与门禁说明见 [§8](#8-新旧-api-对照表28-条)。

## 1. 结论

`/Users/samxx/vscspace/ct-pkg/sqlite/` 下的 `sqlite` 0.1.0 已完成并通过验证：

- 5 项可行性探针全部跑通；其中 2 项存在「语言/映射配方」层面的硬限制，均已用
  `static inline` shim 绕过，并在 NOTES 中明确标注为**补丁，不是语言能力**。
- `ct build` 无警告；生成 C 以 `cc -Wall -Wextra` 编译零警告。
- `ct get ./sqlite` → `ct install` → `ct build` → 运行，demo 输出与
  `test/demo.expected.out` 逐字节一致。
- 只写了 `sqlite/` 目录；未改 ct 编译器、`index/`、`docs/`、`tools/`、顶层文件，
  未 `git init` / commit / push / tag。

## 2. 探针逐条结果

源码与完整记录在 [`NOTES.probe.md`](NOTES.probe.md)，探针源码在
`/tmp/ct-sqlite-probe/`。环境：macOS arm64、`ct v0.19.1`、系统 SQLite
`3.51.0`。

| # | 探针 | 结果 | 关键证据 |
| --- | --- | --- | --- |
| 1 | 函数指针作参数传给 C 函数 | **通过** | `qsort(..., by_int)` 输出 `1 2 3`；`sqlite3_exec` 回调经 shim 传 `userdata`，输出 `row 1: a=x` / `count=1`。 |
| 2 | `void **` / `sqlite3 **` 出参 | **通过（有补丁）** | `void **pp = &p` 与 `sqlite3_open(":memory:", &db)` 均运行通过；但 recipe 内不能写 `(sqlite3 **)db`，报 `map recipe names 'sqlite3' but it is not a binding parameter`。改为 `Sqlite.shim.h` 的 `static inline` 强转。 |
| 3 | 变参透传 | **通过（直接调用）/ 限制（trait）** | `sqlite3_config(SQLITE_CONFIG_LOG, log_cb, 0)` 直接调用输出 `config rc=0`；trait 方法声明 `int config(int op, ...)` 报 `expected a type (tok 4 '...')`，故变参入口只做固定参数 shim。 |
| 4 | 不透明句柄按指针保存、跨单元传递 | **通过** | `handle.h.ct` 中 `void *db` / `void *st` 跨单元返回与调用，输出 `stmt=0x...` / `col=hello`。 |
| 5 | macOS 链接 `-lsqlite3` | **通过** | 头文件与 `libsqlite3.tbd` 位于 `$(xcrun --show-sdk-path)/usr/{include,lib}`；带 `--ldflags '-lsqlite3'` 成功，去掉后 `Undefined symbols ... _sqlite3_open`。 |

附加探针：`long long`、`const char *`、类型宿主
`map trait T for type sqlite3 *`、`.m.ct` 目录约束、shim 自动包含、生成物零
wrapper，均记录在 NOTES。

## 3. 包内文件清单

| 文件 | 作用 |
| --- | --- |
| `ct.pkg` | `name = sqlite`、`version = 0.1.0`、`ldflags = -lsqlite3`、`requires = ct >= 0.20.0`。 |
| `LICENSE` | SQLite public domain + 本包 CT 代码 MIT。 |
| `README.md` | 包定位 / 依赖与门禁 / 安装 / 最小示例 / 文档链接。 |
| `docs/tutorial.md` | API 逐组说明、生命周期、配方、排查与限制。 |
| `NOTES.probe.md` | 5 项探针 + 7 项附加发现的代码、命令与真实输出（文首标注后续变更）。 |
| `sqlite/Sqlite.m.ct` | A 层：`Conn` / `Statement` / `Binding` / `Column` / `Lib` 五个 pub trait 的零成本映射。 |
| `sqlite/Sqlite.shim.h` | B 层：`SqliteDB` / `SqliteStmt` 包装结构体（C tag 为 `ct_sqlite_db` / `ct_sqlite_stmt`）、幂等 `close` / `finalize`、出参与默认 flags 的 `open`、回调转发、`column_text` 窄化。全 `static inline`，无 `.shim.c`。 |
| `test/demo.ct` | 五分钟教程的完整可运行代码。 |
| `test/demo.expected.out` | demo 的期望输出。 |
| `.gitignore` | 排除 `.ctbuild/`。 |

设计要点：

- 公共句柄是本包的包装结构体 `SqliteDB *` / `SqliteStmt *`，内部才存
  `sqlite3 *` / `sqlite3_stmt *`，调用方不必按值暴露 C 类型。
- **不使用 `Drop`**：句柄是可拷贝的普通指针，自动析构会在副本上重复关闭；
  改为显式、幂等的 `close()` / `finalize()`（先 finalize 再 close）。
  幂等由 shim 置空内部指针实现，包装壳本身不释放。
- A 层调用点在生成 C 中直接展开为 shim 的 `static inline` 调用，没有 `ct_*`
  包装符号。

## 4. 真实命令输出

在 `/tmp/sqlite-consumer`（`dep = sqlite@^0.1.0`，`./sqlite` 指向本包）：

```text
$ ct get ./sqlite
vendored 1 package(s)

$ ct install
ct build -I vendor/sqlite --ldflags '-lsqlite3' <unit>.ct -o build

$ ct build -I vendor/sqlite --ldflags '-lsqlite3' src/main.ct -o build
'cc' -std=c99 -I 'vendor/sqlite/sqlite' '.ctbuild/main.c' '.ctbuild/Db.c' '.ctbuild/Stmt.c' -lsqlite3 '-o' 'build'

$ ./build
open=0
create=0
prepare_insert=0
step_insert=101
step_insert=101
prepare_select=0
row 1: id=1 name=alice
row 2: id=2 name=bob
rows=2
closed

$ ct verify
verified 1 package(s)
```

`diff -u test/demo.expected.out /tmp/sqlite-demo.actual`：无差异。

A 层全符号冒烟（临时文件 `/tmp/sqlite-consumer/src/all.ct`，**不入包**）：

```text
$ ct build -I vendor/sqlite --ldflags '-lsqlite3' src/all.ct -o /tmp/all
'cc' -std=c99 -I 'vendor/sqlite/sqlite' '.ctbuild/all.c' -lsqlite3 '-o' '/tmp/all'

$ /tmp/all
open=0
busy=0
exec=0
exec=0
changes=1 rowid=1
prep=0
bind_i=0
bind_i64=0
bind_d=0
bind_t=0
clear=0
bind_t=0
step=100
count=3
name=b type=1
int=7 int64=7 double=1.50 text=x
reset=0
fin=0
cb a=x
exec_cb=0
cb_count=1
errcode=0
errmsg=not an error
libversion=3.51.0
errstr=SQL logic error
close=0
```

零警告验证：

```text
$ cc -Wall -Wextra -std=c99 -I vendor/sqlite -I vendor/sqlite/sqlite \
    .ctbuild/main.c .ctbuild/Db.c .ctbuild/Stmt.c -lsqlite3 -o /tmp/wall-final
exit=0
```

`ct check`（已知限制，未修）：

```text
src/main.ct:4:1: error: 'import' is not supported in compile mode
```

## 5. README 验证覆盖

| README 步骤 | 是否实跑 | 说明 |
| --- | --- | --- |
| §2 `ct get ./sqlite`、`ct install` | **是** | 本地目录方式；输出见上。 |
| §2 `requires` 旧编译器告警 | 否 | 本轮构建统一使用 `ct v0.20.0`，未复测旧编译器告警；条款来自 ct 文档。 |
| §2 索引方式 `ct get sqlite@^0.1.0` | 否 | 任务禁止改 `index/`，0.1.0 尚无索引行；`ct get sqlite` 实际报 git clone 失败。 |
| §3 教程完整代码 | **是** | 代码即 `test/demo.ct`，完整命令链实跑，输出逐字节比对通过。 |
| §3 `ct build` 自动加 `-I vendor/sqlite/sqlite` | **是** | 生成 C 首行 `#include "Sqlite.shim.h"`，cc 行由 ct build 自动带上 shim 目录。 |
| §4 全部符号可编译 | **是** | 所有 trait/函数都在 demo 或探针中参与编译；见下方运行时覆盖。 |
| §5 平台限制 | 部分 | macOS 已验证；Linux 未测（本机无环境），Windows 明确不支持。 |
| §6 FAQ 的 `ct check` / 无 `-lsqlite3` / 静态 trait / 嵌套目录 / finalize 顺序 / `column_text` 生命周期 | **是**（除静态 trait 为探针复现） | 错误文本均取自实际运行。 |
| §7 版本与许可 | — | 0.1.0；未打 tag、未发布索引。 |

运行时覆盖差异：

- `test/demo.ct` 覆盖 11 个方法：`open` / `exec` / `prepare` / `step` / `reset` /
  `finalize` / `bind_int64` / `bind_text` / `column_int64` / `column_text` / `close`。
- 本次收尾另用临时冒烟 `/tmp/sqlite-verify/smoke.ct`（未入库）把**既有 28 个方法全部实跑**：
  `ct build` 通过、运行输出齐全、生成 C 在 `-Wall -Wextra` 下零警告。关键断言：
  连续两次 `close()` / `finalize()` 以及对 `0` 调用都返回 `0`；`bind_text` 的显式长度
  生效（`n = 3` 绑定 `"abcdef"` 得到 `abc`，`n = 5` 绑定含 `\0` 的 `"ab\0cd"` 经
  `hex(?)` 得到 `6162006364`）。逐条对应见 §8。
- 后续新增 `DbPath.openSqliteDb()`（`char *` 宿主，第 29 个入口，旧 API 无对应）：
  `/tmp/open-probe/demo.ct` 分别用 `:memory:`、`/tmp/sqlite-open-probe.db`、非法路径
  `/no/such/dir/...` 各跑一例，前两者返回句柄、非法路径返回 `0`；生成 C 零警告。
  README 快速开始已改用它，demo 仍保留 `Lib.open` 的 rc 路径。
- 因此「没有实跑证据」的方法目前为 0；但入库的回归测试仍只有 demo，另外 17 个方法
  只有本次临时冒烟证据，没有包内可重复执行的测试。
- 未做运行时验证的只剩：文件库上的完整读写（文件打开已由 `openSqliteDb` 探针验证）、
  并发/锁等待、大 BLOB、超长 SQL；见 §7。

## 6. 必须上报的编译能力缺口

1. **`.m.ct` recipe 右侧不能含强转。**
   `sqlite3_open_v2(path, (sqlite3 **)db, flags, 0)` 会报
   `map recipe names 'sqlite3' but it is not a binding parameter`。当前用
   `Sqlite.shim.h` 的 `static inline` 强转补齐；**这是补丁**。
   建议编译器允许 recipe 实参是受限表达式，或提供 out-param 的显式类型标注。
2. **静态 trait 方法不能声明 `...`。**
   `int config(int op, ...)` 直接报 `expected a type (tok 4 '...')`。
   变参 C 函数在 CT 里可以直接调用（透传成立），但无法进入 trait 映射。
   当前 `sqlite3_config` 类接口不映射；若需要，只能再加固定参数 shim。
3. **`.m.ct` 的 namespace 必须等于所在目录。**
   根目录放 `sqlite.m.ct` 报 `namespace 'sqlite' does not match the directory ''`。
   本包因此把 A/B 层放在 `sqlite/Sqlite.m.ct`，教程里写
   `import sqlite::Sqlite;`。这是布局约束，不是运行时缺陷。
4. **静态 trait 没有接口值。**
   `Db d = Db::sqlite;` 报 `a static trait has no interface value`；只能
   `sqlite::Conn.exec(db, sql)` 限定调用。已写入 README FAQ。
5. **命名空间函数在语句开头用 `ns::fn(...)` 会被当成类型声明。**
   探针里 `own::box_close(&c);` 报 `expected a type (tok 4 '::')`；放到表达式
   上下文即可。这是语言能力记录；重构后的 C 层已删除，当前 API 不受影响。
6. **跨单元 `Drop` 不适合本包的所有权模型。**
   句柄是可拷贝的普通指针；`Drop` 会在副本上重复关闭同一句柄。若将来要做
   RAII，编译器需要「不可拷贝的 move 类型」或引用计数支持；0.1.0 用显式
   `close()` / `finalize()`，幂等由 shim 的包装结构体置空内部指针实现。
7. **`ct build` 不读取 `ct.pkg` 的 `ldflags`。**
   这不是编译错误，但要求用户复制 `ct install` 打印的命令。README 已显式
   说明；如果期望 `ct build` 直接从包元数据取链接参数，需要工具侧增强。
8. **`ct check` 不是本包的门禁路径。** 对带 `import` 的文件报
   `'import' is not supported in compile mode`；对包内 `.m.ct` 还会误报
   `a type host is only allowed in a .m.ct unit`（它把 `.m.ct` 当普通单元检查）。
   两条均已实测复现；门禁只走 `ct build`。按任务要求未修，只作为已知限制记录。

## 7. 未验证项与风险

- Linux / Windows：未测。README 已标注 macOS 为已验证平台。
- 索引安装路径：未测（禁止改 `index/`）；本地 `ct get` 已验证。
- 多线程并发、`busy_timeout` 实际等待、`SQLITE_BUSY` 关闭路径：未做压力验证。
- 大 BLOB、超长 SQL、`sqlite3_prepare_v2` 只取单条语句（`tail = 0`）：
  超出 0.1.0 最小面，未覆盖。
- `db_errmsg` 在句柄为 `0` 时返回 SQLite 的 "out of memory" 文案，属于
  SQLite 自身语义，未额外包装。

## 8. 新旧 API 对照表（28 条）

旧签名的来源：重构前的包快照
`/Users/samxx/vscspace/ct-album/vendor/sqlite/Db.h.ct`、`Stmt.h.ct`、
`sqlite/Sqlite.m.ct`，以及 `ct-pkg/docs/phase2.md` §4/§5 的旧接口摘录。
当前包内已没有 `Db.h.ct` / `Stmt.h.ct`，下表不是从现树推断出来的。

判定口径：

- **保留**：语义与失败行为不变，只是调用形状从裸函数变成方法；
- **语义变化**：调用形状或返回类型变了，调用方需要改写；
- **能力删减**：旧接口能做、新接口做不到的事。**本表为 0 条**——`bind_text`
  反而是能力恢复（见 #14）。

| # | 旧入口（快照） | 新调用 | 判定 | demo | 临时冒烟 |
| --- | --- | --- | --- | --- | --- |
| 1 | `db_open(Db *db, const char *path)` | `sqlite::Lib.open(path, &db)` | 语义变化：`Db*` 出参改为包内包装壳出参；失败时同样回填可关闭句柄 | ✓ | ✓ |
| 2 | `db_open_v2(Db *db, const char *path, int flags)` | `sqlite::Lib.open_v2(path, &db, flags)` | 语义变化：同上，显式 flags | ✗ | ✓ |
| 3 | `db_close(Db *db)` → `void`，幂等 | `db.close()` → `int`，幂等 | 语义变化：返回 `close_v2` 码；二次调用与 `0` 句柄仍返回 `SQLITE_OK` | ✓ | ✓ |
| 4 | `db_errmsg(Db *db)` | `db.errmsg()` | 保留 | ✗ | ✓ |
| 5 | `db_errcode(Db *db)` | `db.errcode()` | 保留 | ✗ | ✓ |
| 6 | `db_exec(Db *db, const char *sql)` | `db.exec(sql)` | 保留 | ✓ | ✓ |
| 7 | `db_changes(Db *db)` | `db.changes()` | 保留 | ✗ | ✓ |
| 8 | `db_last_insert_rowid(Db *db)` | `db.last_insert_rowid()` | 保留 | ✗ | ✓ |
| 9 | `stmt_prepare(Db *db, Stmt *st, const char *sql)` | `db.prepare(sql, &st)` | 语义变化：语句出参改为包装壳；不再在 `Stmt` 里保存连接指针 | ✓ | ✓ |
| 10 | `stmt_finalize(Stmt *st)` → `void`，幂等 | `st.finalize()` → `int`，幂等 | 语义变化：返回最后执行错误；二次调用与 `0` 句柄返回 `SQLITE_OK` | ✓ | ✓ |
| 11 | `stmt_step(Stmt *st)` | `st.step()` | 保留 | ✓ | ✓ |
| 12 | `stmt_reset(Stmt *st)` | `st.reset()` | 保留 | ✓ | ✓ |
| 13 | `stmt_clear_bindings(Stmt *st)` | `st.clear_bindings()` | 保留 | ✗ | ✓ |
| 14 | `stmt_bind_text(Stmt *st, int idx, const char *s)`（固定 `n = -1`） | `st.bind_text(idx, s, n)` | **能力恢复**：旧 C 层只能绑 NUL 结尾文本；新接口显式 `n`，可绑含 `\0` 的文本和已知长度的大字段（旧 A 层本就有 `n`） | ✓（`n = -1`） | ✓（`n = -1` / `3` / 含 `\0` 的 `5`） |
| 15 | `stmt_bind_int(Stmt *st, int idx, int v)` | `st.bind_int(idx, v)` | 保留 | ✗ | ✓ |
| 16 | `stmt_bind_int64(Stmt *st, int idx, long long v)` | `st.bind_int64(idx, v)` | 保留 | ✓ | ✓ |
| 17 | `stmt_bind_double(Stmt *st, int idx, double v)` | `st.bind_double(idx, v)` | 保留 | ✗ | ✓ |
| 18 | `stmt_column_count(Stmt *st)` | `st.column_count()` | 保留 | ✗ | ✓ |
| 19 | `stmt_column_name(Stmt *st, int i)` | `st.column_name(i)` | 保留 | ✗ | ✓ |
| 20 | `stmt_column_type(Stmt *st, int i)` | `st.column_type(i)` | 保留 | ✗ | ✓ |
| 21 | `stmt_column_int(Stmt *st, int i)` | `st.column_int(i)` | 保留 | ✗ | ✓ |
| 22 | `stmt_column_int64(Stmt *st, int i)` | `st.column_int64(i)` | 保留 | ✓ | ✓ |
| 23 | `stmt_column_double(Stmt *st, int i)` | `st.column_double(i)` | 保留 | ✗ | ✓ |
| 24 | `stmt_column_text(Stmt *st, int i)` | `st.column_text(i)` | 保留 | ✓ | ✓ |
| 25 | `sqlite::Conn.busy_timeout(void *db, int ms)`（静态命名空间调用） | `db.busy_timeout(ms)` | 语义变化：命名空间调用改为方法调用，行为不变 | ✗ | ✓ |
| 26 | `sqlite::Lib.libversion()` | `sqlite::Lib.libversion()` | 保留 | ✗ | ✓ |
| 27 | `sqlite::Lib.errstr(int code)` | `sqlite::Lib.errstr(code)` | 保留 | ✗ | ✓ |
| 28 | `sqlite::Conn.exec_cb(void *db, const char *sql, ct_sqlite_cb cb, void *ud)` | `sqlite::Lib.exec_cb(db, sql, cb, ud)` | 语义变化：从 `Conn` 挪到 `Lib`；仍保留显式 C 回调边界 | ✗ | ✓ |

覆盖结论：

- `test/demo.ct` 覆盖 #1、#3、#6、#9、#10、#11、#12、#14、#16、#22、#24，共 11 条；
- 其余 17 条本次由临时冒烟 `/tmp/sqlite-verify/smoke.ct` 实跑（未入库），
  因此 28 条都有实跑证据，但入库的回归测试只覆盖 11 条；
- `#14` 的显式长度证据：`bind_text(2, "abcdef", 3)` 后查得 `abc`；
  `bind_text(1, "ab\0cd", 5)` 后 `hex(?)` 得到 `6162006364`（5 字节全被绑定，
  含中间的 NUL）；
- 幂等证据：`close()` / `finalize()` 各连续调用两次均返回 `0`，对 `0` 句柄调用
  同样返回 `0`；
- 上表只覆盖重构前后的 28 条旧→新映射；`DbPath.openSqliteDb()` 是后续新增的
  路径接收者入口，没有旧 API 对应，不在表内；
- 门禁只走 `ct build`：`ct check` 对 `Sqlite.m.ct` 误报
  `a type host is only allowed in a .m.ct unit`，对带 `import` 的 demo 报
  `'import' is not supported in compile mode`，两条均为实测。
