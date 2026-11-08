# sqlite — SQLite 的最小映射包

`sqlite` 把系统 SQLite 的常用 CRUD 接口映射到 CT。它是**映射**，不是数据库：SQLite 负责
存储与执行，本包只提供声明和少量调用形状适配。接口是**方法式**的——连接方法挂在
`SqliteDB *` 上，语句方法挂在 `SqliteStmt *` 上，调用点直接写 `db.exec(sql)`、
`st.bind_int(1, v)`。分两层：

- `sqlite/Sqlite.m.ct` — 零成本静态映射（A 层）：`Conn` 挂 `SqliteDB *`，
  `Statement` / `Binding` / `Column` 挂 `SqliteStmt *`，`DbPath` 挂 `char *`
  （`path.openSqliteDb()`），`Lib` 是无接收者的命名空间入口；
  调用点在生成的 C 里直接展开成 shim 里的 `static inline` 转发；
- `sqlite/Sqlite.shim.h` — header-only 适配（B 层）：句柄包装结构体（C tag 为
  `ct_sqlite_db` / `ct_sqlite_stmt`，CT 侧别名为 `SqliteDB` / `SqliteStmt`）、
  幂等的 `close` / `finalize`、出参、`const unsigned char *` 到
  `const char *` 的窄化、NULL 安全转发。

句柄由本包包装：`SqliteDB *` / `SqliteStmt *` 只是内部 `sqlite3 *` /
`sqlite3_stmt *` 的一层壳，调用方无需 include `sqlite3.h` 也能持有。`close()` /
`finalize()` 是**幂等**的：内部指针被置空，第二次调用返回 `SQLITE_OK`；对 0 调用同样返回
`SQLITE_OK`。代价是包装壳本身不释放（每次 `open` / `prepare` 泄漏一个 8/16 字节的结构体），
用它换二次关闭不触碰已释放内存——与 `std::io` 的 `ct_io_file` 同一取舍。

**能力与边界**

- 能：`open` / `open_v2` / `openSqliteDb` / `close`、`exec`（含回调）、`prepare` / `step` / `reset` /
  `finalize`、`bind_text` / `bind_int` / `bind_int64` / `bind_double`、
  `column_count` / `column_name` / `column_type` / `column_int` / `column_int64` /
  `column_double` / `column_text`、`errcode` / `errmsg`、`changes`、`last_insert_rowid`、
  `busy_timeout`、`libversion`、`errstr`。
- 不能：BLOB、`sqlite3_value`、虚表、session、`load_extension`、变参接口、Windows。
  不 vendored：链接系统 `libsqlite3`，依赖系统头文件。

## 依赖与平台

| 项 | 值 |
| --- | --- |
| `ldflags` | `-lsqlite3`（必须；不带会在链接期报 `Undefined symbols ... _sqlite3_open`） |
| `cflags` | 无 |
| 平台 | macOS arm64 + 系统 SQLite `3.51.0` 已验证；Linux（`libsqlite3-dev`）预期可用但未实测；Windows 不支持 |
| 编译器 | `requires = ct >= 0.20.0`（软约束，不满足只告警） |

`ct build` 不读取 `ct.pkg`，`--ldflags '-lsqlite3'` 必须显式照抄。

门禁只走 `ct build`：`ct check` 对包内 `.m.ct` 会误报「type host 只允许出现在 .m.ct
单元」，对带 `import` 的入口也会报 `'import' is not supported in compile mode`。

## 安装

包名 `sqlite`，版本 `0.1.0`。当前工作树未写入 `index/`，用本地目录安装：

```sh
ct get ./sqlite            # 或 ct get file:///path/to/ct-pkg/sqlite
ct install
```

工程 `ct.pkg` 加一行依赖：

```ini
dep = sqlite@^0.1.0
```

`ct install` 离线校验 `vendor/` 与 `ct.lock`，打印：

```text
ct build -I vendor/sqlite --ldflags '-lsqlite3' <unit>.ct -o build
```

把 `<unit>.ct` 换成入口文件；`-o` 的父目录要先 `mkdir -p build`。

## 最小可运行示例

`src/main.ct`：内存库建表、插入两行、查询并遍历结果。错误检查为简洁省略，完整写法见教程。

```ct
#include <sqlite3.h>
#include <stdio.h>

import sqlite::Sqlite;

int main(void)
{
    char *path = ":memory:";
    SqliteDB *db = path.openSqliteDb();
    SqliteStmt *st = 0;

    if (db == 0) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }

    db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT)");
    db.exec("INSERT INTO t(name) VALUES('alice'), ('bob')");

    db.prepare("SELECT id, name FROM t ORDER BY id", &st);
    while (st.step() == SQLITE_ROW) {
        printf("%d %s\n", st.column_int(0), st.column_text(1));
    }
    st.finalize();
    db.close();
    return 0;
}
```

构建并运行：

```sh
mkdir -p build
ct build -I vendor/sqlite --ldflags '-lsqlite3' src/main.ct -o build/main
./build/main
```

```text
1 alice
2 bob
```

`path.openSqliteDb()` 成功返回句柄，失败返回 `0`；字面量也可直接当接收者
（`":memory:".openSqliteDb()`）。需要即时 SQLite 错误码时仍用
`sqlite::Lib.open(path, &db)`，它返回 `rc`。

## 术语表（Glossary）

术语按三类收录：本包特有概念、全仓易混概念、正文出现过但未解释的英文术语。
类型名、方法名、文件名与源码逐字一致。

| 术语 | 含义 | 本包中的所指 |
| --- | --- | --- |
| `SqliteDB` | CT 可见的连接句柄类型，内部只包一个 `sqlite3 *`。 | `pub type SqliteDB = struct ct_sqlite_db;`，定义在 `sqlite/Sqlite.m.ct`；`Conn` 的方法（`exec` / `prepare` / `close` 等）都挂在 `SqliteDB *` 上。 |
| `SqliteStmt` | CT 可见的语句句柄类型，内部只包一个 `sqlite3_stmt *`。 | `pub type SqliteStmt = struct ct_sqlite_stmt;`；`Statement` / `Binding` / `Column` 三组方法都挂在 `SqliteStmt *` 上。 |
| db 与 stmt 的所有权（handle ownership） | 两个句柄各自负责什么、谁负责释放；本包没有 `Drop`，必须显式关闭。 | 外壳（`ct_sqlite_db` / `ct_sqlite_stmt`）由本包 `malloc` 且永不释放；内部指针由 `close()` / `finalize()` 释放；顺序硬约束是先 `finalize()` 语句、再 `close()` 连接。 |
| prepared statement | 由 `prepare` 编译好的可重复执行语句对象，绑定参数与执行分离。 | `db.prepare(sql, &st)` 产出的 `SqliteStmt *`；同一语句可反复 `bind_text` / `bind_int` / `bind_int64` / `bind_double`、`step`、`reset`。 |
| `reset` | 把语句退回未执行状态，但保留当前绑定值。 | `SqliteStmt.reset()`，转发 `sqlite3_reset`；要清空绑定需再调 `clear_bindings()`。 |
| `clear_bindings` | 清空语句上的全部参数绑定。 | `SqliteStmt.clear_bindings()`，转发 `sqlite3_clear_bindings`。 |
| 列索引与绑定索引（column / bind index） | 列索引从 0 开始；绑定索引从 1 开始，因为绑定方法的序号就是 SQL 里 `?` 的位次。 | `column_count` / `column_name` / `column_type` / `column_int` / `column_int64` / `column_double` / `column_text` 的 `i` 从 0 数；`bind_text` / `bind_int` / `bind_int64` / `bind_double` 的 `idx` 从 1 数，越界返回 `SQLITE_RANGE`。 |
| 事务（transaction） | 一组要么全部生效、要么全部回滚的语句。 | 本包没有专用 API，用 `db.exec("BEGIN")` / `"COMMIT"` / `"ROLLBACK"` 执行；错误码原样从 `exec` 返回。 |
| `BLOB` | SQLite 的二进制大对象列类型，按原始字节存取。 | 本包不映射 `bind_blob` / `column_blob`；`column_text` 读 BLOB 只按字节解释，不能原样往返。 |
| `rowid` | SQLite 为每行维护的隐式 64 位整数主键。 | `last_insert_rowid()` 返回最近一次 INSERT 生成的 rowid；表里没有 `INTEGER PRIMARY KEY` 时它仍然存在。 |
| `SQLITE_BUSY` / `SQLITE_LOCKED` | 数据库被其它连接锁住（BUSY）或同一连接内冲突（LOCKED）时的错误码。 | 由 `exec` / `step` / `prepare` 原样返回；等待可用 `busy_timeout(ms)` 设置重试超时。 |
| `SQLITE_*` 状态码 | SQLite 的原始 `int` 状态码，如 `SQLITE_OK`(0)、`SQLITE_ROW`(100)、`SQLITE_DONE`(101)。 | 本包不定义自己的错误码，所有方法直接返回 `sqlite3_*` 的 `int`；幂等的 `close()` / `finalize()` 对 0 或已关闭句柄返回 `SQLITE_OK`。 |
| `SQLITE_TRANSIENT` | 告诉 SQLite「复制这份绑定数据」的标志。 | `bind_text` 内部传给 `sqlite3_bind_text`，因此绑定后调用方缓冲区可以立刻复用。 |
| `close` / `finalize` 幂等 | 重复调用不报错、不重复释放。 | 内部指针被置空，第二次调用返回 `SQLITE_OK`；外壳不释放，代价是每次 `open` / `prepare` 留下一个 8/16 字节结构体。 |
| 句柄（handle） | 一个不透明指针，调用方只通过方法操作它，不直接碰内部结构。 | `SqliteDB *` 与 `SqliteStmt *`；C 侧的 `struct ct_sqlite_db` / `struct ct_sqlite_stmt` 不出包。 |
| 宿主（host） | trait 方法挂靠的类型。 | 三个类型宿主 `SqliteDB *` / `SqliteStmt *` / `char *`（`DbPath`），加一个命名空间宿主 `namespace sqlite`（`Lib`）。 |
| 映射（map trait） | 把 trait 方法绑定到宿主并转调 C 函数的声明。 | `sqlite/Sqlite.m.ct` 的 `Conn` / `Statement` / `Binding` / `Column` / `Lib` / `DbPath` 六段 `map trait … for …`，每行转调一个 `ct_sqlite_*` 函数。 |
| 链式返回自身（return receiver） | 方法返回接收者本身，使下一次调用可以接在后面。 | 本包不提供：方法返回 SQLite 状态码、`const char *` 或 `long long`；`openSqliteDb()` 返回新句柄。 |
| 所有权（ownership） | 谁负责释放一块内存的约定。 | 外壳归本包且永不释放，内部指针由 `close()` / `finalize()` 释放；详见「db 与 stmt 的所有权」一条。 |
| 借用（borrow） | 只取得指针的使用权，不承担释放责任。 | `column_text` / `errmsg` / `column_name` / `errstr` / `libversion` 返回借用指针：或只在下一次 `step` / `reset` / `finalize` 前有效，或由 SQLite 静态持有，都不要 `free`。 |

## 完整文档

API 逐组说明（方法签名、语义、返回值、失败情形）、句柄与语句生命周期、5 个可运行配方、
错误排查和全部限制，见 [docs/tutorial.md](docs/tutorial.md)。

- 许可：SQLite 本身为 public domain；本包的 CT 代码、shim、README 与 demo 为 MIT，
  见 [LICENSE](LICENSE)。
- 版本：`0.1.0`；未打 tag、未写入 `index/`。
