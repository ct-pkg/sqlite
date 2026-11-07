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

## 完整文档

API 逐组说明（方法签名、语义、返回值、失败情形）、句柄与语句生命周期、5 个可运行配方、
错误排查和全部限制，见 [docs/tutorial.md](docs/tutorial.md)。

- 许可：SQLite 本身为 public domain；本包的 CT 代码、shim、README 与 demo 为 MIT，
  见 [LICENSE](LICENSE)。
- 版本：`0.1.0`；未打 tag、未写入 `index/`。
